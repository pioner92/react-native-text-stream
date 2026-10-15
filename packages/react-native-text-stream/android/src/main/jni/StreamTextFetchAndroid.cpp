// streamFetch() on Android: the platform side of cpp/StreamTextFetch.hpp, over com.textstream.StreamTextFetch.
#include "StreamTextFetchAndroid.h"

#include "../../../../ios/TextStreamBuffer.h"
#include "StreamTextFetch.hpp"

#include <fbjni/ByteBuffer.h>
#include <algorithm>
#include <climits>
#include <vector>

namespace facebook::react {

namespace {

/** What the Kotlin side reports to, from its callback thread; freed by `nativeComplete`. */
struct FetchState {
  std::shared_ptr<StreamTextMappedBuffer> buffer;
  std::shared_ptr<StreamTextFetchListener> listener;
  std::unique_ptr<StreamTextResponseParser> parser;
  uint64_t lastNotified{0};
  // Allocated only for parsed transports; raw fetches retain their zero-allocation body path.
  std::vector<jbyte> parserScratch;
};

FetchState *stateOf(jlong handle) {
  return reinterpret_cast<FetchState *>(handle);
}

/** Publishes bytes OkHttp already read into the data in place (TSBHeader.committed, release). */
void publish(void *base, uint64_t committed) {
  __atomic_store_n(&static_cast<TSBHeader *>(base)->committed, committed, __ATOMIC_RELEASE);
}

std::string toStdString(jni::alias_ref<jstring> string) {
  return string ? string->toStdString() : std::string();
}

} // namespace

class AndroidFetchTask : public StreamTextFetchTask {
 public:
  explicit AndroidFetchTask(jni::global_ref<JStreamTextFetch> fetch) : fetch_(std::move(fetch)) {}

  ~AndroidFetchTask() override {
    jni::ThreadScope scope;
    fetch_.reset();
  }

  void cancel() override {
    jni::ThreadScope scope;
    static const auto method = JStreamTextFetch::javaClassStatic()->getMethod<void()>("cancel");
    method(fetch_);
  }

 private:
  jni::global_ref<JStreamTextFetch> fetch_;
};

void JStreamTextFetch::nativeResponse(
    jni::alias_ref<jclass>,
    jlong handle,
    jint status,
    jni::alias_ref<jni::JArrayClass<jstring>> headers,
    jni::alias_ref<jstring> url) {
  StreamTextHeaders pairs;
  size_t length = headers ? headers->size() : 0;
  for (size_t i = 0; i + 1 < length; i += 2) {
    pairs.emplace_back(toStdString(headers->getElement(i)), toStdString(headers->getElement(i + 1)));
  }
  stateOf(handle)->listener->onResponse(status, std::move(pairs), toStdString(url));
}

void JStreamTextFetch::nativePublish(jni::alias_ref<jclass>, jlong handle, jlong committed) {
  auto &buffer = stateOf(handle)->buffer;
  publish(buffer->data(), uint64_t(committed));
  // Wakes the views showing it (no polling).
  buffer->notify();
}

jint JStreamTextFetch::nativeConsume(
    jni::alias_ref<jclass>,
    jlong handle,
    jni::alias_ref<jni::JArrayByte> bytes,
    jint count) {
  auto *state = stateOf(handle);
  if (!state->parser || !bytes || count < 0 || static_cast<size_t>(count) > bytes->size()) {
    return static_cast<jint>(StreamTextParseStatus::Error);
  }
  if (static_cast<size_t>(count) > state->parserScratch.size()) {
    return static_cast<jint>(StreamTextParseStatus::Error);
  }
  if (count > 0) bytes->getRegion(0, count, state->parserScratch.data());
  StreamTextParseStatus status = StreamTextParseStatus::Error;
  try {
    status = state->parser->feed(
        reinterpret_cast<const uint8_t *>(state->parserScratch.data()),
        static_cast<size_t>(count));
  } catch (...) {
    status = StreamTextParseStatus::Error;
  }
  const uint64_t committed = tsb_committed(state->buffer->data());
  if (status == StreamTextParseStatus::Continue && committed != state->lastNotified) {
    state->lastNotified = committed;
    state->buffer->notify();
  }
  return static_cast<jint>(status);
}

void JStreamTextFetch::nativeComplete(jni::alias_ref<jclass>, jlong handle, jint status, jni::alias_ref<jstring> error) {
  std::unique_ptr<FetchState> state(stateOf(handle));
  StreamTextParseStatus parserStatus = StreamTextParseStatus::Continue;
  if (status == TSB_STATUS_DONE && state->parser) {
    try { parserStatus = state->parser->finish(); } catch (...) { parserStatus = StreamTextParseStatus::Error; }
    if (parserStatus == StreamTextParseStatus::Error) status = TSB_STATUS_ERROR;
    else if (parserStatus == StreamTextParseStatus::Overflow) status = TSB_STATUS_OVERFLOW;
  }
  // finish() is the one terminal notification. In particular, finish() may have appended a
  // final complete record, so it must publish status and bytes together rather than waking twice.
  state->buffer->finish(uint32_t(status));
  std::string message = toStdString(error);
  if (message.empty() && status == TSB_STATUS_ERROR) {
    if (state->parser) message = state->parser->error();
    if (message.empty()) message = "streamFetch: response parser failed";
  }
  state->listener->onComplete(std::move(message));
}

void JStreamTextFetch::registerNatives() {
  javaClassStatic()->registerNatives({
      makeNativeMethod("nativeResponse", JStreamTextFetch::nativeResponse),
      makeNativeMethod("nativePublish", JStreamTextFetch::nativePublish),
      makeNativeMethod("nativeConsume", JStreamTextFetch::nativeConsume),
      makeNativeMethod("nativeComplete", JStreamTextFetch::nativeComplete),
  });
}

std::shared_ptr<StreamTextFetchTask> streamTextStartFetch(
    StreamTextFetchRequest request,
    std::shared_ptr<StreamTextMappedBuffer> buffer,
    std::shared_ptr<StreamTextFetchListener> listener) {
  jni::ThreadScope scope;
  auto *base = buffer->data();
  // A Java ByteBuffer holds at most INT_MAX bytes: past them the body overflows.
  size_t capacity = std::min<size_t>(buffer->size() - TSB_HEADER_SIZE, INT_MAX);
  auto data = jni::JByteBuffer::wrapBytes(base + TSB_HEADER_SIZE, capacity);
  auto headers = jni::JArrayClass<jstring>::newArray(request.headers.size() * 2);
  for (size_t i = 0; i < request.headers.size(); i++) {
    headers->setElement(i * 2, jni::make_jstring(request.headers[i].first).get());
    headers->setElement(i * 2 + 1, jni::make_jstring(request.headers[i].second).get());
  }
  jni::local_ref<jni::JArrayByte> body;
  if (request.body) {
    // The body is already UTF-8. A JNI string conversion may truncate at an embedded NUL.
    body = jni::JArrayByte::newArray(request.body->size());
    body->setRegion(0, request.body->size(), reinterpret_cast<const jbyte *>(request.body->data()));
  }
  std::unique_ptr<FetchState> state;
  try {
    state = std::make_unique<FetchState>();
    state->buffer = buffer;
    state->listener = listener;
    if (request.parser.protocol != StreamTextProtocol::Text) {
      FetchState *sinkState = state.get();
      state->parserScratch.resize(8192);
      state->parser = std::make_unique<StreamTextResponseParser>(request.parser, [sinkState](std::string_view text) {
        if (text.empty()) return true;
        void *base = sinkState->buffer->data();
        const auto *header = static_cast<const TSBHeader *>(base);
        const uint64_t committed = tsb_committed(base);
        const uint64_t remaining = header->capacity - committed;
        const uint64_t prefix = std::min<uint64_t>(remaining, text.size());
        if (prefix != 0) tsb_append(base, text.data(), prefix);
        // The parser turns false into its Overflow state after keeping the prefix that fitted.
        return prefix == text.size();
      });
    }
    static const auto start = JStreamTextFetch::javaClassStatic()
                                  ->getStaticMethod<jni::local_ref<JStreamTextFetch>(
                                      jlong,
                                      jni::alias_ref<jstring>,
                                      jni::alias_ref<jstring>,
                                      jni::alias_ref<jni::JArrayClass<jstring>>,
                                      jni::alias_ref<jni::JArrayByte>,
                                      jlong,
                                      jni::alias_ref<jni::JByteBuffer>,
                                      jboolean)>("start");
    auto fetch = start(
        JStreamTextFetch::javaClassStatic(),
        reinterpret_cast<jlong>(state.get()),
        jni::make_jstring(request.url),
        jni::make_jstring(request.method),
        headers,
        body,
        jlong(request.timeoutSeconds * 1000),
        data,
        request.parser.protocol != StreamTextProtocol::Text ? JNI_TRUE : JNI_FALSE);
    state.release(); // Kotlin owns and releases this handle through nativeComplete.
    // A global ref/task allocation is the only fallible work after Kotlin may have completed
    // synchronously. Do not enter the start-up failure path then: it would complete the same
    // listener twice. Cancel the returned Java callback instead; nativeComplete owns the state.
    try {
      return std::make_shared<AndroidFetchTask>(jni::make_global(fetch));
    } catch (...) {
      try {
        static const auto cancel = JStreamTextFetch::javaClassStatic()->getMethod<void()>("cancel");
        cancel(fetch);
      } catch (...) {}
      return nullptr;
    }
  } catch (const std::exception &e) {
    // Kotlin never took the handle.
    buffer->finish(TSB_STATUS_ERROR);
    listener->onComplete(std::string("streamFetch: ") + e.what());
    return nullptr;
  }
}

} // namespace facebook::react
