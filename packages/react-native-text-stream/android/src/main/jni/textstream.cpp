// Android side of the C++: the JNI the Kotlin view (com.textstream) reads its buffer through, and
// what the shared shadow node needs from the platform.
#include "textstream.h"

#include "../../../../ios/TextStreamBuffer.h"
#include "StreamTextBuffers.hpp"
#include "StreamTextFetchAndroid.h"
#include "StreamTextPreparedAndroid.h"
#include "StreamTextViewShadowNode.hpp"

#include <fbjni/ByteBuffer.h>
#include <fbjni/fbjni.h>
#include <react/fabric/StateWrapperImpl.h>

namespace facebook::react {

std::shared_ptr<TurboModule> textstream_ModuleProvider(
    const std::string & /*moduleName*/,
    const JavaTurboModule::InitParams & /*params*/) {
  return nullptr;
}

namespace {

using StreamTextConcreteState = StreamTextViewShadowNode::ConcreteState;

/** `com.textstream.StreamTextSource`: keeps a TSB1 buffer alive while the view reads it. */
class JStreamTextSource : public jni::HybridClass<JStreamTextSource> {
 public:
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextSource;";

  static jni::local_ref<javaobject> create(std::shared_ptr<jsi::MutableBuffer> buffer) {
    return newObjectCxxArgs(std::move(buffer));
  }

  const std::shared_ptr<jsi::MutableBuffer> &buffer() const {
    return buffer_;
  }

  static void registerNatives() {
    registerHybrid({
        makeNativeMethod("address", JStreamTextSource::address),
        makeNativeMethod("size", JStreamTextSource::size),
        makeNativeMethod("bytes", JStreamTextSource::bytes),
    });
  }

 private:
  friend HybridBase;

  explicit JStreamTextSource(std::shared_ptr<jsi::MutableBuffer> buffer) : buffer_(std::move(buffer)) {}

  jlong address() {
    return reinterpret_cast<jlong>(buffer_->data());
  }

  jlong size() {
    return static_cast<jlong>(buffer_->size());
  }

  /** The whole buffer, header included; read the data only below `committed`. */
  jni::local_ref<jni::JByteBuffer> bytes() {
    return jni::JByteBuffer::wrapBytes(buffer_->data(), buffer_->size());
  }

  std::shared_ptr<jsi::MutableBuffer> buffer_;
};

std::shared_ptr<const StreamTextConcreteState> stateOf(jni::alias_ref<jobject> stateWrapper) {
  if (!stateWrapper || !stateWrapper->isInstanceOf(StateWrapperImpl::javaClassStatic())) {
    return nullptr;
  }
  auto wrapper = jni::static_ref_cast<StateWrapperImpl::javaobject>(stateWrapper);
  return std::static_pointer_cast<const StreamTextConcreteState>(wrapper->cthis()->getState());
}

/** `com.textstream.StreamTextNative`. */
struct JStreamTextNative : jni::JavaClass<JStreamTextNative> {
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextNative;";

  /** The buffer of an `attach` command (StreamTextBuffers), or null. */
  static jni::local_ref<JStreamTextSource::javaobject> takeBuffer(jni::alias_ref<jclass>, jint id) {
    auto buffer = id > 0 ? StreamTextBufferRegistry::shared().take(id) : nullptr;
    return buffer ? JStreamTextSource::create(std::move(buffer)) : nullptr;
  }

  /** The `source` prop as of this state, or null. */
  static jni::local_ref<JStreamTextSource::javaobject> stateSource(jni::alias_ref<jclass>, jni::alias_ref<jobject> stateWrapper) {
    auto state = stateOf(stateWrapper);
    if (!state || !state->getData().source) {
      return nullptr;
    }
    return JStreamTextSource::create(state->getData().source);
  }

  static jint stateGeneration(jni::alias_ref<jclass>, jni::alias_ref<jobject> stateWrapper) {
    auto state = stateOf(stateWrapper);
    return state ? state->getData().sourceGeneration : 0;
  }

  /**
   * The view's height for what it shows now, applied on this (main) thread: the default mode
   * would wait for the JS thread. It wins until the `source` prop changes.
   */
  static void updateHeight(jni::alias_ref<jclass>, jni::alias_ref<jobject> stateWrapper, jfloat height, jint revision) {
    auto state = stateOf(stateWrapper);
    if (!state) {
      return;
    }
    state->updateState(
        [height, revision](const StreamTextViewState &old) {
          auto next = std::make_shared<StreamTextViewState>(old);
          next->height = height;
          next->revision = revision;
          next->propSource = old.source ? old.source->data() : nullptr;
          next->prepared = nullptr;
          return next;
        },
        EventQueue::UpdateMode::unstable_Immediate);
  }

  /** The layout the shadow node prepared for this state's `source`, if any. */
  static jni::local_ref<JStreamTextPreparedLayout::javaobject> statePrepared(
      jni::alias_ref<jclass>,
      jni::alias_ref<jobject> stateWrapper) {
    auto state = stateOf(stateWrapper);
    return state ? streamTextPreparedLayoutOf(state->getData().prepared, state->getData().source) : nullptr;
  }

  /** Lays `source` out in the background for its next mount (a view leaving that streamed it). */
  static void prepareInBackground(
      jni::alias_ref<jclass>,
      jni::alias_ref<JStreamTextSource::javaobject> source,
      jfloat width,
      jfloat fontSize,
      jni::alias_ref<jstring> fontFamily,
      jni::alias_ref<jstring> fontWeight,
      jfloat lineHeight,
      jfloat letterSpacing,
      jni::alias_ref<jstring> textAlign) {
    if (!source) {
      return;
    }
    auto text = [](jni::alias_ref<jstring> value) { return value ? value->toStdString() : std::string(); };
    streamTextPrepareInBackground(
        source->cthis()->buffer(), width, fontSize, text(fontFamily), text(fontWeight), lineHeight, letterSpacing,
        text(textAlign));
  }

  static void setPreparedEnabled(jni::alias_ref<jclass>, jboolean enabled) {
    streamTextPreparedSetEnabled(enabled);
  }

  static void clearPreparedCache(jni::alias_ref<jclass>, jboolean invalidate) {
    streamTextPreparedCacheClear(invalidate);
  }

  static jint preparedCacheCount(jni::alias_ref<jclass>) {
    return streamTextPreparedCacheCount();
  }

  static jni::local_ref<jstring> preparedCacheDump(jni::alias_ref<jclass>) {
    return jni::make_jstring(streamTextPreparedCacheDump());
  }

  /** A valid TSB1 header that fits in `size` bytes. */
  static jboolean isValid(jni::alias_ref<jclass>, jlong base, jlong size) {
    if (size < TSB_HEADER_SIZE) {
      return false;
    }
    auto header = reinterpret_cast<const TSBHeader *>(base);
    return header->magic == TSB_MAGIC && header->version == TSB_VERSION &&
        header->capacity <= static_cast<uint64_t>(size - TSB_HEADER_SIZE);
  }

  static jlong capacity(jni::alias_ref<jclass>, jlong base) {
    return static_cast<jlong>(reinterpret_cast<const TSBHeader *>(base)->capacity);
  }

  static jlong committed(jni::alias_ref<jclass>, jlong base) {
    return static_cast<jlong>(tsb_committed(reinterpret_cast<const void *>(base)));
  }

  static jint status(jni::alias_ref<jclass>, jlong base) {
    return static_cast<jint>(tsb_status(reinterpret_cast<const void *>(base)));
  }

  static void registerNatives() {
    javaClassStatic()->registerNatives({
        makeNativeMethod("takeBuffer", JStreamTextNative::takeBuffer),
        makeNativeMethod("stateSource", JStreamTextNative::stateSource),
        makeNativeMethod("stateGeneration", JStreamTextNative::stateGeneration),
        makeNativeMethod("updateHeight", JStreamTextNative::updateHeight),
        makeNativeMethod("statePrepared", JStreamTextNative::statePrepared),
        makeNativeMethod("setPreparedEnabled", JStreamTextNative::setPreparedEnabled),
        makeNativeMethod("prepareInBackground", JStreamTextNative::prepareInBackground),
        makeNativeMethod("clearPreparedCache", JStreamTextNative::clearPreparedCache),
        makeNativeMethod("preparedCacheCount", JStreamTextNative::preparedCacheCount),
        makeNativeMethod("preparedCacheDump", JStreamTextNative::preparedCacheDump),
        makeNativeMethod("isValid", JStreamTextNative::isValid),
        makeNativeMethod("capacity", JStreamTextNative::capacity),
        makeNativeMethod("committed", JStreamTextNative::committed),
        makeNativeMethod("status", JStreamTextNative::status),
    });
  }
};

} // namespace

} // namespace facebook::react

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  return facebook::jni::initialize(vm, [] {
    facebook::react::JStreamTextSource::registerNatives();
    facebook::react::JStreamTextNative::registerNatives();
    facebook::react::JStreamTextFetch::registerNatives();
    // Looked up here, with the app's class loader: the prepare threads use it without one.
    facebook::react::JStreamTextPreparedLayout::javaClassStatic();
  });
}
