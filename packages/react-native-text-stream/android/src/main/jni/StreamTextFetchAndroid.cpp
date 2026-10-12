// streamFetch() on Android: the platform side of cpp/StreamTextFetch.hpp, over com.textstream.StreamTextFetch.
#include "StreamTextFetchAndroid.h"

#include "../../../../ios/TextStreamBuffer.h"
#include "StreamTextFetch.hpp"

#include <fbjni/ByteBuffer.h>
#include <climits>

namespace facebook::react {

namespace {

/** What the Kotlin side reports to, from its callback thread; freed by `nativeComplete`. */
struct FetchState {
  std::shared_ptr<StreamTextMappedBuffer> buffer;
  std::shared_ptr<StreamTextFetchListener> listener;
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
  publish(stateOf(handle)->buffer->data(), uint64_t(committed));
}

void JStreamTextFetch::nativeComplete(jni::alias_ref<jclass>, jlong handle, jint status, jni::alias_ref<jstring> error) {
  std::unique_ptr<FetchState> state(stateOf(handle));
  tsb_set_status(state->buffer->data(), uint32_t(status));
  state->listener->onComplete(toStdString(error));
}

void JStreamTextFetch::registerNatives() {
  javaClassStatic()->registerNatives({
      makeNativeMethod("nativeResponse", JStreamTextFetch::nativeResponse),
      makeNativeMethod("nativePublish", JStreamTextFetch::nativePublish),
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
  jni::local_ref<jni::JString> body;
  if (request.body) {
    body = jni::make_jstring(*request.body);
  }
  auto *state = new FetchState{buffer, listener};
  try {
    static const auto start = JStreamTextFetch::javaClassStatic()
                                  ->getStaticMethod<jni::local_ref<JStreamTextFetch>(
                                      jlong,
                                      jni::alias_ref<jstring>,
                                      jni::alias_ref<jstring>,
                                      jni::alias_ref<jni::JArrayClass<jstring>>,
                                      jni::alias_ref<jstring>,
                                      jlong,
                                      jni::alias_ref<jni::JByteBuffer>)>("start");
    auto fetch = start(
        JStreamTextFetch::javaClassStatic(),
        reinterpret_cast<jlong>(state),
        jni::make_jstring(request.url),
        jni::make_jstring(request.method),
        headers,
        body,
        jlong(request.timeoutSeconds * 1000),
        data);
    return std::make_shared<AndroidFetchTask>(jni::make_global(fetch));
  } catch (const std::exception &e) {
    // Kotlin never took the handle.
    delete state;
    tsb_set_status(base, TSB_STATUS_ERROR);
    listener->onComplete(std::string("streamFetch: ") + e.what());
    return nullptr;
  }
}

} // namespace facebook::react
