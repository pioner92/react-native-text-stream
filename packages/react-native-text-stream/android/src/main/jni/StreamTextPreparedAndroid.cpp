// Prepared layouts on Android, the counterpart of ios/fabric/StreamTextPrepared.mm: a finished
// `source` of up to 64 KB is laid out in the shadow node's measure (a StaticLayout made by
// com.textstream.StreamTextPreparedLayout), so the view draws it without laying it out at mount.
// Layouts are cached (a list scrolling back, a chat reopened), made once when asked for twice, and
// started early, on two background threads, when React creates the props of a new message.
#include "StreamTextPreparedAndroid.h"

#include "../../../../ios/TextStreamBuffer.h"

#include <fbjni/ByteBuffer.h>
#include <pthread.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <list>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

namespace facebook::react {

namespace {

constexpr uint64_t kMaxTextBytes = 64 * 1024;
// Layouts for texts mounted again; most recently used first.
constexpr size_t kMaxEntries = 64;
// Text bytes; a layout takes several times that (UTF-16 text, lines, display lists).
constexpr uint64_t kMaxCacheBytes = 512 * 1024;

std::atomic<bool> gEnabled{true};
std::atomic<int> gGeneration{0};

/** The props a layout depends on, besides the text and the width (the color is drawn, not laid out). */
struct PreparedStyle {
  Float fontSize;
  std::string fontFamily;
  std::string fontWeight;
  Float lineHeight;
  Float letterSpacing;
  std::string textAlign;
  std::string format;
  std::string markdownFlavor;
  int32_t color{static_cast<int32_t>(0xff000000u)};

  PreparedStyle(
      Float fontSize,
      std::string fontFamily,
      std::string fontWeight,
      Float lineHeight,
      Float letterSpacing,
      std::string textAlign,
      std::string format = {},
      std::string markdownFlavor = {},
      int32_t color = static_cast<int32_t>(0xff000000u))
      : fontSize(fontSize),
        fontFamily(std::move(fontFamily)),
        fontWeight(std::move(fontWeight)),
        lineHeight(lineHeight),
        letterSpacing(letterSpacing),
        textAlign(std::move(textAlign)), format(format == "markdown" ? "markdown" : "plain"),
        markdownFlavor(format == "markdown" && markdownFlavor == "gfm" ? "gfm" : "commonmark"), color(color) {}

  explicit PreparedStyle(const StreamTextViewProps &p)
      : fontSize(p.fontSize),
        fontFamily(p.fontFamily),
        fontWeight(p.fontWeight),
        lineHeight(p.lineHeight),
        letterSpacing(p.letterSpacing),
        textAlign(p.textAlign), format(p.format == "markdown" ? "markdown" : "plain"),
        markdownFlavor(p.format == "markdown" && p.markdownFlavor == "gfm" ? "gfm" : "commonmark"),
        color(isColorMeaningful(p.color) ? *p.color : 0xff000000) {}

  bool operator==(const PreparedStyle &o) const {
    return fontSize == o.fontSize && fontFamily == o.fontFamily && fontWeight == o.fontWeight &&
        lineHeight == o.lineHeight && letterSpacing == o.letterSpacing && textAlign == o.textAlign &&
        format == o.format && markdownFlavor == o.markdownFlavor;
  }
};

/** Committed bytes of a finished buffer small enough to show in one go, or 0 when it isn't one. */
uint64_t finishedBytes(const std::shared_ptr<jsi::MutableBuffer> &buffer) {
  if (!buffer || buffer->size() < TSB_HEADER_SIZE) {
    return 0;
  }
  const void *base = buffer->data();
  const auto *header = static_cast<const TSBHeader *>(base);
  if (header->magic != TSB_MAGIC || header->version != TSB_VERSION ||
      header->capacity > buffer->size() - TSB_HEADER_SIZE) {
    return 0;
  }
  // Status first: once it is terminal, the committed value read after it is final.
  if (tsb_status(base) == TSB_STATUS_OPEN) {
    return 0;
  }
  uint64_t committed = std::min(tsb_committed(base), header->capacity);
  return committed <= kMaxTextBytes ? committed : 0;
}

/** The text's identity: equal texts share a layout, whatever buffer holds them. */
uint64_t contentHash(const std::shared_ptr<jsi::MutableBuffer> &buffer, uint64_t bytes) {
  const char *text = reinterpret_cast<const char *>(buffer->data()) + TSB_HEADER_SIZE;
  return std::hash<std::string_view>{}(std::string_view(text, static_cast<size_t>(bytes)));
}

bool sameWidth(Float a, Float b, Float tolerance = 0.001) {
  return std::fabs(a - b) <= tolerance;
}

/** The UI thread: on Android its id is the process id. */
bool isMainThread() {
  return gettid() == getpid();
}

} // namespace

struct StreamTextPreparedHandle {
  /** The buffer it was made from, only to skip hashing it again while that wrapper lives. */
  std::weak_ptr<jsi::MutableBuffer> buffer;
  uint64_t hash;
  uint64_t bytes;
  PreparedStyle style;
  /** Dp: the width measure asked for, and the layout's height. */
  Float width;
  Float height;
  int generation;
  jni::global_ref<JStreamTextPreparedLayout::javaobject> layout;

  StreamTextPreparedHandle(
      std::weak_ptr<jsi::MutableBuffer> buffer,
      uint64_t hash,
      uint64_t bytes,
      PreparedStyle style,
      Float width,
      Float height,
      int generation,
      jni::global_ref<JStreamTextPreparedLayout::javaobject> layout)
      : buffer(std::move(buffer)),
        hash(hash),
        bytes(bytes),
        style(std::move(style)),
        width(width),
        height(height),
        generation(generation),
        layout(std::move(layout)) {}

  ~StreamTextPreparedHandle() {
    // The last reference may go on any thread.
    jni::ThreadScope scope;
    layout.reset();
  }

  /** Made from this text: the same live buffer, or the same bytes in another one. */
  bool sameText(const std::shared_ptr<jsi::MutableBuffer> &source, uint64_t sourceBytes) const {
    bool sameBuffer = !buffer.owner_before(source) && !source.owner_before(buffer) && !buffer.expired();
    return bytes == sourceBytes && (sameBuffer || hash == contentHash(source, sourceBytes));
  }

  bool matches(
      const std::shared_ptr<jsi::MutableBuffer> &source,
      uint64_t sourceBytes,
      const PreparedStyle &other,
      Float otherWidth,
      Float tolerance = 0.001) const {
    return generation == gGeneration.load() && sameWidth(width, otherWidth, tolerance) && style == other &&
        sameText(source, sourceBytes);
  }
};

namespace {

std::mutex gMutex;
std::list<StreamTextPreparedRef> gEntries;

/** Under gMutex. Drops what a benchmark cleared; moves a hit to the front. */
StreamTextPreparedRef findLocked(uint64_t hash, uint64_t bytes, const PreparedStyle &style, Float width) {
  int generation = gGeneration.load();
  for (auto it = gEntries.begin(); it != gEntries.end();) {
    const auto &entry = **it;
    if (entry.generation != generation) {
      it = gEntries.erase(it);
    } else if (entry.bytes == bytes && entry.hash == hash && sameWidth(entry.width, width) && entry.style == style) {
      auto hit = *it;
      gEntries.splice(gEntries.begin(), gEntries, it);
      return hit;
    } else {
      ++it;
    }
  }
  return nullptr;
}

/** Under gMutex. Inserts at the front and drops the least recently used past the limits. */
void insertLocked(StreamTextPreparedRef entry) {
  gEntries.push_front(std::move(entry));
  uint64_t total = 0;
  for (const auto &e : gEntries) {
    total += e->bytes;
  }
  while (gEntries.size() > 1 && (gEntries.size() > kMaxEntries || total > kMaxCacheBytes)) {
    total -= gEntries.back()->bytes;
    gEntries.pop_back();
  }
}

/** The width measure last asked for: an early start guesses the bubble width from it. */
std::atomic<Float> gLastWidth{0};

// Widths measure asked for lately (under gMutex): a view knows its width only in whole pixels, a
// layout made for it must be keyed by the width the next measure will ask for.
std::list<Float> gRecentWidths;

/** Under gMutex. */
void rememberWidthLocked(Float width) {
  for (auto it = gRecentWidths.begin(); it != gRecentWidths.end(); ++it) {
    if (*it == width) {
      gRecentWidths.erase(it);
      break;
    }
  }
  gRecentWidths.push_front(width);
  if (gRecentWidths.size() > 8) {
    gRecentWidths.pop_back();
  }
}

/** Under gMutex: the recent measure width within a pixel or so of `width`, else `width`. */
Float snapWidthLocked(Float width) {
  for (Float recent : gRecentWidths) {
    if (std::fabs(recent - width) <= 0.5) {
      return recent;
    }
  }
  return width;
}

struct Job {
  std::shared_ptr<jsi::MutableBuffer> source;
  uint64_t bytes;
  uint64_t hash;
  PreparedStyle style;
  Float width;
};

struct InFlight {
  uint64_t hash;
  uint64_t bytes;
  PreparedStyle style;
  Float width;
  std::shared_future<StreamTextPreparedRef> result;
};

// Layouts being made right now, so a second request waits instead of laying out the same text.
std::list<InFlight> gInFlight;

/** Under gMutex. */
std::shared_future<StreamTextPreparedRef> *findInFlightLocked(const Job &job) {
  for (auto &f : gInFlight) {
    if (f.hash == job.hash && f.bytes == job.bytes && sameWidth(f.width, job.width) && f.style == job.style) {
      return &f.result;
    }
  }
  return nullptr;
}

/** Under gMutex: registers the job as in flight. */
std::shared_ptr<std::promise<StreamTextPreparedRef>> startLocked(const Job &job) {
  auto promise = std::make_shared<std::promise<StreamTextPreparedRef>>();
  gInFlight.push_back(InFlight{job.hash, job.bytes, job.style, job.width, promise->get_future().share()});
  return promise;
}

jni::local_ref<jstring> javaString(const std::string &value) {
  return value.empty() ? nullptr : jni::make_jstring(value);
}

/** Lays the text out (StreamTextPreparedLayout.create); null if Java threw. Attached thread. */
StreamTextPreparedRef makeLayout(const Job &job, int generation) {
  static const auto create = JStreamTextPreparedLayout::javaClassStatic()
                                 ->getStaticMethod<JStreamTextPreparedLayout::javaobject(
                                     jni::alias_ref<jni::JByteBuffer>,
                                     jint,
                                     jfloat,
                                     jfloat,
                                     jni::alias_ref<jstring>,
                                     jni::alias_ref<jstring>,
                                     jfloat,
                                     jfloat,
                                     jint,
                                     jni::alias_ref<jstring>,
                                     jni::alias_ref<jstring>,
                                     jni::alias_ref<jstring>)>("create");
  static const auto getHeight = JStreamTextPreparedLayout::javaClassStatic()->getMethod<jfloat()>("getHeight");
  try {
    auto data = jni::JByteBuffer::wrapBytes(job.source->data(), job.source->size());
    auto layout = create(
        JStreamTextPreparedLayout::javaClassStatic(),
        data,
        static_cast<jint>(job.bytes),
        job.width,
        job.style.fontSize,
        javaString(job.style.fontFamily),
        javaString(job.style.fontWeight),
        job.style.lineHeight,
        job.style.letterSpacing, static_cast<jint>(job.style.color),
        javaString(job.style.textAlign), javaString(job.style.format), javaString(job.style.markdownFlavor));
    if (!layout) {
      return nullptr;
    }
    Float height = getHeight(layout);
    return std::make_shared<const StreamTextPreparedHandle>(
        job.source, job.hash, job.bytes, job.style, job.width, height, generation, jni::make_global(layout));
  } catch (const std::exception &) {
    return nullptr;
  }
}

/**
 * Makes the layout, caches it, settles waiters. On the main thread too (a rotation): without one
 * the view lays the text out there anyway.
 */
StreamTextPreparedRef run(const Job &job, const std::shared_ptr<std::promise<StreamTextPreparedRef>> &promise) {
  auto entry = makeLayout(job, gGeneration.load());
  {
    std::lock_guard lock(gMutex);
    if (entry) {
      insertLocked(entry);
    }
    for (auto it = gInFlight.begin(); it != gInFlight.end(); ++it) {
      if (it->hash == job.hash && it->bytes == job.bytes && sameWidth(it->width, job.width) && it->style == job.style) {
        gInFlight.erase(it);
        break;
      }
    }
  }
  promise->set_value(entry);
  return entry;
}

/**
 * Two threads: many bubbles laying out at once contend on the font caches and take longer in all
 * than a couple of lanes. Attached to the JVM for their lifetime.
 */
class PrepareQueue {
 public:
  static PrepareQueue &shared() {
    static PrepareQueue *queue = new PrepareQueue();
    return *queue;
  }

  void add(std::function<void()> work) {
    {
      std::lock_guard lock(mutex_);
      jobs_.push_back(std::move(work));
    }
    ready_.notify_one();
  }

 private:
  PrepareQueue() {
    // As on iOS. Four were twice as slow for 20 texts on a Pixel 8a.
    for (int i = 0; i < 2; i++) {
      std::thread([this] {
        // Named apart from the JS thread it would otherwise inherit its name from.
        pthread_setname_np(pthread_self(), "StreamTextPrep");
        jni::ThreadScope::WithClassLoader([this] { loop(); });
      }).detach();
    }
  }

  void loop() {
    while (true) {
      std::function<void()> work;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return !jobs_.empty(); });
        work = std::move(jobs_.front());
        jobs_.pop_front();
      }
      work();
    }
  }

  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::function<void()>> jobs_;
};

/** Starts making the layout on the prepare threads unless it is cached or already being made. */
void startInBackground(const std::shared_ptr<jsi::MutableBuffer> &source, PreparedStyle style, Float width) {
  if (!gEnabled.load() || !(width > 0)) {
    return;
  }
  uint64_t bytes = finishedBytes(source);
  if (bytes == 0) {
    return;
  }
  Job job{source, bytes, contentHash(source, bytes), std::move(style), width};
  std::shared_ptr<std::promise<StreamTextPreparedRef>> promise;
  {
    std::lock_guard lock(gMutex);
    if (findLocked(job.hash, bytes, job.style, width) || findInFlightLocked(job)) {
      return;
    }
    promise = startLocked(job);
  }
  PrepareQueue::shared().add([job = std::move(job), promise] { run(job, promise); });
}

} // namespace

StreamTextPreparedRef
streamTextPrepare(const StreamTextViewProps &props, Float width, const StreamTextPreparedRef &current) {
  if (!gEnabled.load() || !(width > 0)) {
    return nullptr;
  }
  uint64_t bytes = finishedBytes(props.source);
  if (bytes == 0) {
    return nullptr;
  }
  PreparedStyle style(props);
  if (current && current->matches(props.source, bytes, style, width)) {
    return current;
  }
  bool main = isMainThread();
  if (!main) {
    gLastWidth.store(width, std::memory_order_relaxed);
  }
  Job job{props.source, bytes, contentHash(props.source, bytes), style, width};
  std::shared_future<StreamTextPreparedRef> waiting;
  std::shared_ptr<std::promise<StreamTextPreparedRef>> mine;
  {
    std::lock_guard lock(gMutex);
    if (auto hit = findLocked(job.hash, bytes, style, width)) {
      return hit;
    }
    // The same layout already being made (an early start): wait for it.
    if (auto inFlight = findInFlightLocked(job)) {
      waiting = *inFlight;
    } else {
      mine = startLocked(job);
    }
  }
  if (waiting.valid()) {
    // The main thread never queues behind background work: unless it is done, the state height serves.
    if (main && waiting.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      return nullptr;
    }
    return waiting.get();
  }
  // Parsing Markdown and building spans are never paid by Fabric's main-thread measure. The view
  // uses its current height until this prepared result is published on a later state update.
  if (main) {
    PrepareQueue::shared().add([job = std::move(job), mine] { run(job, mine); });
    return nullptr;
  }
  return run(job, mine);
}

Float streamTextPreparedHeight(const StreamTextPreparedRef &prepared) {
  return prepared ? prepared->height : 0;
}

Float streamTextPreparedWidth(const StreamTextPreparedRef &prepared) {
  return prepared ? prepared->width : 0;
}

bool streamTextPreparedFits(const StreamTextPreparedRef &prepared, const StreamTextViewProps &props, Float width) {
  uint64_t bytes = prepared ? finishedBytes(props.source) : 0;
  // Yoga rounds frames to the pixel grid: the layout it measured still sizes and draws this frame.
  return bytes > 0 && prepared->matches(props.source, bytes, PreparedStyle(props), width, 0.5);
}

void streamTextPrepareEarly(const StreamTextViewProps &props) {
  // React builds the props of a new message in its render phase, before the commit lays it out:
  // start laying it out now, at the width bubbles were last measured at, so measure finds it done
  // (or nearly) and many bubbles lay out on several cores at once.
  startInBackground(props.source, PreparedStyle(props), gLastWidth.load(std::memory_order_relaxed));
}

void streamTextNoteMeasureWidth(Float width) {
  if (width > 0) {
    std::lock_guard lock(gMutex);
    rememberWidthLocked(width);
  }
}

void streamTextPrepareInBackground(
    const std::shared_ptr<jsi::MutableBuffer> &source,
    Float width,
    Float fontSize,
    std::string fontFamily,
    std::string fontWeight,
    Float lineHeight,
    Float letterSpacing,
    std::string textAlign,
    std::string format,
    std::string markdownFlavor,
    int32_t color) {
  {
    std::lock_guard lock(gMutex);
    width = snapWidthLocked(width);
  }
  startInBackground(
      source,
      PreparedStyle(fontSize, std::move(fontFamily), std::move(fontWeight), lineHeight, letterSpacing, std::move(textAlign), std::move(format), std::move(markdownFlavor), color),
      width);
}

jni::local_ref<JStreamTextPreparedLayout::javaobject> streamTextPreparedLayoutOf(
    const StreamTextPreparedRef &prepared,
    const std::shared_ptr<jsi::MutableBuffer> &source) {
  uint64_t bytes = prepared ? finishedBytes(source) : 0;
  if (bytes == 0 || !prepared->sameText(source, bytes)) {
    return nullptr;
  }
  return jni::make_local(prepared->layout);
}

void streamTextPreparedSetEnabled(bool enabled) {
  gEnabled.store(enabled);
}

void streamTextPreparedCacheClear(bool invalidate) {
  if (invalidate) {
    gGeneration.fetch_add(1);
  }
  std::list<StreamTextPreparedRef> dropped;
  {
    std::lock_guard lock(gMutex);
    dropped.swap(gEntries);
  }
  // Released outside the lock (their Java layouts go with them).
}

int streamTextPreparedCacheCount() {
  std::lock_guard lock(gMutex);
  int live = 0;
  int generation = gGeneration.load();
  for (const auto &entry : gEntries) {
    live += entry->generation == generation;
  }
  return live;
}

std::string streamTextPreparedCacheDump() {
  std::lock_guard lock(gMutex);
  std::ostringstream out;
  for (const auto &e : gEntries) {
    out << "hash=" << std::hex << e->hash << std::dec << " gen=" << e->generation << "/" << gGeneration.load()
        << " bytes=" << e->bytes << " width=" << e->width << " height=" << e->height << " font=" << e->style.fontSize
        << "\n";
  }
  return out.str();
}

} // namespace facebook::react
