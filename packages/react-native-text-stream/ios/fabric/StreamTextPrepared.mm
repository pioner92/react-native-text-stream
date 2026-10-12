#import "StreamTextPrepared.h"

#import <React/RCTConversions.h>

#include "../TextStreamBuffer.h"
#include <cmath>
#include <atomic>
#include <future>
#include <list>
#include <mutex>
#include <string_view>

using namespace facebook;
using namespace facebook::react;

static NSString *_Nullable StreamTextNSString(const std::string &value) {
  return value.empty() ? nil : RCTNSStringFromString(value);
}

StreamTextStyle *StreamTextStyleFromProps(const StreamTextViewProps &props) {
  StreamTextStyle *style = [StreamTextStyle new];
  style.fontSize = props.fontSize;
  style.fontFamily = StreamTextNSString(props.fontFamily);
  style.fontWeight = StreamTextNSString(props.fontWeight);
  style.textColor = RCTUIColorFromSharedColor(props.color);
  style.lineHeight = props.lineHeight;
  style.letterSpacing = props.letterSpacing;
  style.textAlign = StreamTextNSString(props.textAlign);
  return style;
}

namespace {

/** The props a layout depends on, besides the text and the width. */
struct PreparedStyle {
  Float fontSize;
  std::string fontFamily;
  std::string fontWeight;
  SharedColor color;
  Float lineHeight;
  Float letterSpacing;
  std::string textAlign;

  explicit PreparedStyle(const StreamTextViewProps &p)
      : fontSize(p.fontSize),
        fontFamily(p.fontFamily),
        fontWeight(p.fontWeight),
        color(p.color),
        lineHeight(p.lineHeight),
        letterSpacing(p.letterSpacing),
        textAlign(p.textAlign) {}

  /** The color only matters to a layout that has it baked in. */
  bool sameLayout(const PreparedStyle &o, bool colorMatters) const {
    return fontSize == o.fontSize && fontFamily == o.fontFamily && fontWeight == o.fontWeight &&
        (!colorMatters || color == o.color) && lineHeight == o.lineHeight && letterSpacing == o.letterSpacing &&
        textAlign == o.textAlign;
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
  return committed <= StreamTextPreparedLayout.maxBytes ? committed : 0;
}

/** The text's identity: equal texts share a layout, whatever buffer holds them. ~30 µs for 64 KB. */
uint64_t contentHash(const std::shared_ptr<jsi::MutableBuffer> &buffer, uint64_t bytes) {
  const char *text = reinterpret_cast<const char *>(buffer->data()) + TSB_HEADER_SIZE;
  return std::hash<std::string_view>{}(std::string_view(text, static_cast<size_t>(bytes)));
}

} // namespace

namespace facebook::react {

struct StreamTextPreparedHandle {
  /** The buffer it was made from, only to skip hashing it again while that wrapper lives. */
  std::weak_ptr<jsi::MutableBuffer> buffer;
  uint64_t hash;
  PreparedStyle style;
  StreamTextPreparedLayout *layout;
  NSInteger generation;

  /** Made from this text: the same live buffer, or the same bytes in another one. */
  bool sameText(const std::shared_ptr<jsi::MutableBuffer> &source, uint64_t bytes) const {
    bool sameBuffer = !buffer.owner_before(source) && !source.owner_before(buffer) && !buffer.expired();
    return layout.bytes == bytes && (sameBuffer || hash == contentHash(source, bytes));
  }

  /** Same text and style; lines are only reused at exactly the same width. */
  bool matches(const std::shared_ptr<jsi::MutableBuffer> &source, uint64_t bytes, const PreparedStyle &other,
               Float width, Float tolerance = 0.001) const {
    return generation == StreamTextPreparedLayout.generation && std::fabs(layout.width - width) <= tolerance &&
        style.sameLayout(other, !layout.colorIndependent) && sameText(source, bytes);
  }
};

} // namespace facebook::react

namespace {

// Layouts for texts mounted again (a list scrolling back, a chat reopened); most recently used first.
constexpr size_t kMaxEntries = 64;
// Text bytes; a layout takes several times that (UTF-16 string, glyphs, lines).
constexpr uint64_t kMaxBytes = 512 * 1024;
std::mutex gMutex;
std::list<StreamTextPreparedRef> gEntries;

/** Under gMutex. Drops what a benchmark cleared; moves a hit to the front. */
StreamTextPreparedRef findLocked(uint64_t hash, uint64_t bytes, const PreparedStyle &style, Float width) {
  for (auto it = gEntries.begin(); it != gEntries.end();) {
    const auto &entry = **it;
    if (entry.generation != StreamTextPreparedLayout.generation) {
      it = gEntries.erase(it);
    } else if (entry.layout.bytes == bytes && entry.hash == hash && std::fabs(entry.layout.width - width) <= 0.001 &&
               entry.style.sameLayout(style, !entry.layout.colorIndependent)) {
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
  static dispatch_once_t observing;
  dispatch_once(&observing, ^{
    // Under memory pressure the cache goes first.
    [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
                                                    object:nil
                                                     queue:nil
                                                usingBlock:^(NSNotification *) {
                                                  StreamTextPreparedCacheClear();
                                                }];
  });
  gEntries.push_front(std::move(entry));
  uint64_t total = 0;
  for (const auto &e : gEntries) {
    total += e->layout.bytes;
  }
  while (gEntries.size() > 1 && (gEntries.size() > kMaxEntries || total > kMaxBytes)) {
    total -= gEntries.back()->layout.bytes;
    gEntries.pop_back();
  }
}

/** The width measure last asked for: an early start guesses the bubble width from it. */
std::atomic<Float> gLastWidth{0};

/** One layout to make: the text, its identity and where it goes. */
struct Job {
  std::shared_ptr<jsi::MutableBuffer> source;
  uint64_t bytes;
  uint64_t hash;
  PreparedStyle style;
  Float width;
  StreamTextStyle *textStyle;
};

struct InFlight {
  uint64_t hash;
  uint64_t bytes;
  PreparedStyle style;
  Float width;
  std::shared_future<StreamTextPreparedRef> result;
};

// Layouts being made right now, so a second request waits instead of typesetting the same text.
std::list<InFlight> gInFlight;

/** Under gMutex. */
std::shared_future<StreamTextPreparedRef> *findInFlightLocked(const Job &job) {
  for (auto &f : gInFlight) {
    if (f.hash == job.hash && f.bytes == job.bytes && std::fabs(f.width - job.width) <= 0.001 &&
        f.style.sameLayout(job.style, true)) {
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

/**
 * Makes the layout, caches it, settles waiters. On the main thread (a rotation, Split View) too:
 * without one the view lays the text out there anyway, in a text view, which costs more.
 */
StreamTextPreparedRef run(Job job, std::shared_ptr<std::promise<StreamTextPreparedRef>> promise, bool main) {
  StreamTextPreparedRef entry;
  CFTimeInterval started = CACurrentMediaTime();
  StreamTextPreparedLayout *layout;
  @autoreleasepool {
    layout = [[StreamTextPreparedLayout alloc]
        initWithUtf8:static_cast<const uint8_t *>(job.source->data()) + TSB_HEADER_SIZE
               count:static_cast<NSInteger>(job.bytes)
               bytes:job.bytes
               style:job.textStyle
               width:job.width];
  }
  if (main) {
    StreamTextPreparedLayout.mainLayouts += 1;
    StreamTextPreparedLayout.mainLongest = std::max(StreamTextPreparedLayout.mainLongest, CACurrentMediaTime() - started);
  }
  if (layout) {
    entry = std::make_shared<const StreamTextPreparedHandle>(
        StreamTextPreparedHandle{job.source, job.hash, job.style, layout, StreamTextPreparedLayout.generation});
  }
  {
    std::lock_guard lock(gMutex);
    if (entry) {
      insertLocked(entry);
    }
    for (auto it = gInFlight.begin(); it != gInFlight.end(); ++it) {
      if (it->hash == job.hash && it->bytes == job.bytes && std::fabs(it->width - job.width) <= 0.001 &&
          it->style.sameLayout(job.style, true)) {
        gInFlight.erase(it);
        break;
      }
    }
  }
  promise->set_value(entry);
  return entry;
}

} // namespace

namespace facebook::react {

StreamTextPreparedRef
streamTextPrepare(const StreamTextViewProps &props, Float width, const StreamTextPreparedRef &current) {
  if (!StreamTextPreparedLayout.enabled || !(width > 0)) {
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
  if (!NSThread.isMainThread) {
    gLastWidth.store(width, std::memory_order_relaxed);
  }
  Job job{props.source, bytes, contentHash(props.source, bytes), style, width, StreamTextStyleFromProps(props)};
  std::shared_future<StreamTextPreparedRef> waiting;
  std::shared_ptr<std::promise<StreamTextPreparedRef>> mine;
  {
    std::lock_guard lock(gMutex);
    if (auto hit = findLocked(job.hash, bytes, style, width)) {
      return hit;
    }
    // The same layout already being made (an early start, a background prepare): wait for it.
    if (auto inFlight = findInFlightLocked(job)) {
      waiting = *inFlight;
    } else {
      mine = startLocked(job);
    }
  }
  if (waiting.valid()) {
    // The main thread never queues behind background work: unless it is done, the state height serves.
    if (NSThread.isMainThread && waiting.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      return nullptr;
    }
    return waiting.get();
  }
  return run(job, std::move(mine), NSThread.isMainThread);
}

Float streamTextPreparedHeight(const StreamTextPreparedRef &prepared) {
  return prepared ? static_cast<Float>(prepared->layout.height) : 0;
}

Float streamTextPreparedWidth(const StreamTextPreparedRef &prepared) {
  return prepared ? static_cast<Float>(prepared->layout.width) : 0;
}

bool streamTextPreparedFits(const StreamTextPreparedRef &prepared, const StreamTextViewProps &props, Float width) {
  uint64_t bytes = prepared ? finishedBytes(props.source) : 0;
  // Yoga rounds frames to the pixel grid: the layout it measured still sizes and draws this frame.
  return bytes > 0 && prepared->matches(props.source, bytes, PreparedStyle(props), width, 0.5);
}

} // namespace facebook::react

StreamTextPreparedLayout *_Nullable StreamTextPreparedLayoutOf(
    const StreamTextPreparedRef &prepared,
    const std::shared_ptr<jsi::MutableBuffer> &source) {
  uint64_t bytes = prepared ? finishedBytes(source) : 0;
  return bytes > 0 && prepared->sameText(source, bytes) ? prepared->layout : nil;
}

namespace {

// Two at a time: twenty bubbles typesetting at once contend on CoreText's font caches and take
// longer in all than a couple of lanes (a long text still spreads its paragraphs over the cores).
NSOperationQueue *prepareQueue() {
  static NSOperationQueue *queue = [] {
    NSOperationQueue *q = [NSOperationQueue new];
    q.name = @"StreamText.prepare";
    q.maxConcurrentOperationCount = 2;
    q.qualityOfService = NSQualityOfServiceUserInitiated;
    return q;
  }();
  return queue;
}

/** Starts making the layout on the prepare queue unless it is cached or already being made. */
void startInBackground(const StreamTextViewProps &props, Float width) {
  if (!StreamTextPreparedLayout.enabled || !(width > 0)) {
    return;
  }
  uint64_t bytes = finishedBytes(props.source);
  if (bytes == 0) {
    return;
  }
  Job job{props.source, bytes, contentHash(props.source, bytes), PreparedStyle(props), width, StreamTextStyleFromProps(props)};
  std::shared_ptr<std::promise<StreamTextPreparedRef>> promise;
  {
    std::lock_guard lock(gMutex);
    if (findLocked(job.hash, bytes, job.style, width) || findInFlightLocked(job)) {
      return;
    }
    promise = startLocked(job);
  }
  [prepareQueue() addOperationWithBlock:^{
    run(job, promise, false);
  }];
}

} // namespace

namespace facebook::react {

void streamTextPrepareEarly(const StreamTextViewProps &props) {
  // React builds the props of a new message in its render phase, before the commit lays it out:
  // start typesetting it now, at the width bubbles were last measured at, so measure finds it done
  // (or nearly) and many bubbles typeset on several cores at once.
  startInBackground(props, gLastWidth.load(std::memory_order_relaxed));
}

} // namespace facebook::react

void StreamTextPrepareInBackground(std::shared_ptr<const StreamTextViewProps> props, CGFloat width) {
  if (props) {
    startInBackground(*props, width);
  }
}

void StreamTextPreparedCacheClear() {
  std::lock_guard lock(gMutex);
  gEntries.clear();
}

NSInteger StreamTextPreparedCacheCount() {
  std::lock_guard lock(gMutex);
  NSInteger live = 0;
  for (const auto &entry : gEntries) {
    live += entry->generation == StreamTextPreparedLayout.generation;
  }
  return live;
}

__attribute__((constructor)) static void StreamTextPreparedExposeCache() {
  StreamTextPreparedLayout.cacheCount = ^NSInteger {
    return StreamTextPreparedCacheCount();
  };
  StreamTextPreparedLayout.cacheDump = ^NSString * {
    std::lock_guard lock(gMutex);
    NSMutableString *out = [NSMutableString new];
    for (const auto &e : gEntries) {
      [out appendFormat:@"hash=%llx gen=%ld/%ld bytes=%llu width=%.2f height=%.1f font=%.1f ct=%d\n",
                        e->hash, (long)e->generation,
                        (long)StreamTextPreparedLayout.generation, e->layout.bytes, e->layout.width, e->layout.height,
                        e->style.fontSize, (int)e->layout.isCoreText];
    }
    return out;
  };
  StreamTextPreparedLayout.cacheClear = ^{
    StreamTextPreparedCacheClear();
  };
}
