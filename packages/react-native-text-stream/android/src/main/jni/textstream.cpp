// Android side of the C++: the JNI the Kotlin view (com.textstream) reads its buffer through, and
// what the shared shadow node needs from the platform.
#include "textstream.h"

#include "../../../../ios/TextStreamBuffer.h"
#include "StreamTextBuffers.hpp"
#include "StreamTextFetch.hpp"
#include "StreamTextStreams.hpp"
#include "StreamTextFetchAndroid.h"
#include "StreamTextPreparedAndroid.h"
#include "../../../../cpp/markdown/StreamMarkdown.h"
#include "StreamTextViewShadowNode.hpp"

#include <fbjni/ByteBuffer.h>
#include <fbjni/NativeRunnable.h>
#include <fbjni/fbjni.h>
#include <react/fabric/StateWrapperImpl.h>

#include <climits>
#include <memory>
#include <vector>

namespace facebook::react {

std::shared_ptr<TurboModule> textstream_ModuleProvider(
    const std::string & /*moduleName*/,
    const JavaTurboModule::InitParams & /*params*/) {
  return nullptr;
}

namespace {

using StreamTextConcreteState = StreamTextViewShadowNode::ConcreteState;

/** Resolved while the app class loader is available; parser workers use the cached class. */
struct JStreamTextMarkdown : jni::JavaClass<JStreamTextMarkdown> {
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextMarkdown;";
};

struct MarkdownParserHolder {
  STMarkdownParser *parser = nullptr;
  ~MarkdownParserHolder() { if (parser) stmd_destroy(parser); }
};

jstring utf8String(JNIEnv *env, const char *value) {
  std::vector<jchar> out;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p;) {
    uint32_t cp = *p++;
    if (cp < 0x80) { out.push_back(static_cast<jchar>(cp)); continue; }
    int extra = (cp & 0xe0) == 0xc0 ? 1 : (cp & 0xf0) == 0xe0 ? 2 : (cp & 0xf8) == 0xf0 ? 3 : -1;
    if (extra < 0) { out.push_back(0xfffd); continue; }
    cp &= extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07;
    bool valid = true;
    for (int i = 0; i < extra; ++i) {
      unsigned char next = *p++;
      if ((next & 0xc0) != 0x80) { valid = false; break; }
      cp = (cp << 6) | (next & 0x3f);
    }
    if (!valid || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) { out.push_back(0xfffd); continue; }
    if (cp <= 0xffff) out.push_back(static_cast<jchar>(cp));
    else { cp -= 0x10000; out.push_back(static_cast<jchar>(0xd800 + (cp >> 10))); out.push_back(static_cast<jchar>(0xdc00 + (cp & 0x3ff))); }
  }
  return env->NewString(out.data(), static_cast<jsize>(out.size()));
}

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
        makeNativeMethod("isPushed", JStreamTextSource::isPushed),
        makeNativeMethod("serial", JStreamTextSource::serial),
        makeNativeMethod("observe", JStreamTextSource::observe),
        makeNativeMethod("unobserve", JStreamTextSource::unobserve),
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

  /** Its writer (streamFetch) wakes the view on every write: nothing needs to poll it. */
  jboolean isPushed() {
    return std::dynamic_pointer_cast<StreamTextMappedBuffer>(buffer_) != nullptr;
  }

  /** A streamFetch() buffer's unique number, or 0. */
  jlong serial() {
    auto mapped = std::dynamic_pointer_cast<StreamTextMappedBuffer>(buffer_);
    return mapped ? static_cast<jlong>(mapped->serial()) : 0;
  }

  /** `wake` runs on the writer's thread after each write; 0 if the buffer is not pushed. */
  jlong observe(jni::alias_ref<jni::JRunnable> wake) {
    auto mapped = std::dynamic_pointer_cast<StreamTextMappedBuffer>(buffer_);
    if (!mapped) {
      return 0;
    }
    auto runnable = std::make_shared<jni::global_ref<jni::JRunnable>>(jni::make_global(wake));
    return static_cast<jlong>(mapped->observe([runnable] {
      jni::ThreadScope scope;
      static const auto run = jni::JRunnable::javaClassStatic()->getMethod<void()>("run");
      run(*runnable);
    }));
  }

  void unobserve(jlong token) {
    if (auto mapped = std::dynamic_pointer_cast<StreamTextMappedBuffer>(buffer_)) {
      mapped->unobserve(static_cast<uint64_t>(token));
    }
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

  /** What streams under `streamId` now (streamFetch), or null. */
  static jni::local_ref<JStreamTextSource::javaobject> streamSource(jni::alias_ref<jclass>, jni::alias_ref<jstring> streamId) {
    std::shared_ptr<jsi::MutableBuffer> buffer = StreamTextStreams::shared().find(streamId->toStdString());
    return buffer ? JStreamTextSource::create(std::move(buffer)) : nullptr;
  }

  /** `wake` runs, on any thread, whenever a request starts streaming under `streamId`. */
  static jlong listenStream(jni::alias_ref<jclass>, jni::alias_ref<jstring> streamId, jni::alias_ref<jni::JRunnable> wake) {
    auto runnable = std::make_shared<jni::global_ref<jni::JRunnable>>(jni::make_global(wake));
    return static_cast<jlong>(StreamTextStreams::shared().listen(streamId->toStdString(), [runnable] {
      jni::ThreadScope scope;
      static const auto run = jni::JRunnable::javaClassStatic()->getMethod<void()>("run");
      run(*runnable);
    }));
  }

  static void unlistenStream(jni::alias_ref<jclass>, jni::alias_ref<jstring> streamId, jlong token) {
    StreamTextStreams::shared().unlisten(streamId->toStdString(), static_cast<uint64_t>(token));
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

  static jint stateSourceOwner(jni::alias_ref<jclass>, jni::alias_ref<jobject> stateWrapper) {
    auto state = stateOf(stateWrapper);
    return state ? state->getData().sourceOwner : 0;
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
      jni::alias_ref<jstring> textAlign,
      jni::alias_ref<jstring> format,
      jni::alias_ref<jstring> markdownFlavor,
      jint color) {
    if (!source) {
      return;
    }
    auto text = [](jni::alias_ref<jstring> value) { return value ? value->toStdString() : std::string(); };
    streamTextPrepareInBackground(
        source->cthis()->buffer(), width, fontSize, text(fontFamily), text(fontWeight), lineHeight, letterSpacing,
        text(textAlign), text(format), text(markdownFlavor), color);
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

  /** MD4C output materialized as primitive arrays; invoked only on StreamTextMarkdown workers. */
  static jni::local_ref<JStreamTextMarkdown> parseMarkdown(
      jni::alias_ref<jclass>,
      jni::alias_ref<jstring> markdown,
      jboolean github) {
    if (!markdown) return nullptr;
    const auto input = markdown->toStdString();
    // The fixed Markdown worker pool owns at most two of these. Keeping MD4C's vectors avoids
    // reallocating parser state for every coalesced stream update; thread teardown destroys it.
    thread_local MarkdownParserHolder holder;
    STMarkdownParser *parser = holder.parser;
    if (!parser) parser = holder.parser = stmd_create();
    if (!parser) return nullptr;
    const STMarkdownDocument *document = stmd_parse_decoded(
        parser, input.data(), input.size(), github == JNI_TRUE);
    if (!document || document->textLength > static_cast<size_t>(INT32_MAX) ||
        document->runCount > static_cast<size_t>(INT32_MAX)) {
      return nullptr;
    }
    if (document->error != 0) return nullptr;
    JNIEnv *env = jni::Environment::current();
    const jsize count = static_cast<jsize>(document->runCount);
    jstring text = env->NewString(reinterpret_cast<const jchar *>(document->text), static_cast<jsize>(document->textLength));
    jintArray starts = env->NewIntArray(count);
    jintArray lengths = env->NewIntArray(count);
    jintArray flags = env->NewIntArray(count);
    jintArray headings = env->NewIntArray(count);
    jintArray indents = env->NewIntArray(count);
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray urls = env->NewObjectArray(count, stringClass, nullptr);
    std::vector<jint> startsOut(count), lengthsOut(count), flagsOut(count), headingsOut(count), indentsOut(count);
    for (jsize i = 0; i < count; ++i) {
      const auto &run = document->runs[i];
      startsOut[i] = static_cast<jint>(std::min(run.start, static_cast<size_t>(INT32_MAX)));
      lengthsOut[i] = static_cast<jint>(std::min(run.length, static_cast<size_t>(INT32_MAX)));
      flagsOut[i] = static_cast<jint>(run.flags);
      headingsOut[i] = static_cast<jint>(run.headingLevel);
      indentsOut[i] = static_cast<jint>(run.indent);
      if (run.url) {
        jstring url = utf8String(env, run.url);
        env->SetObjectArrayElement(urls, i, url);
        env->DeleteLocalRef(url);
      }
    }
    env->SetIntArrayRegion(starts, 0, count, startsOut.data());
    env->SetIntArrayRegion(lengths, 0, count, lengthsOut.data());
    env->SetIntArrayRegion(flags, 0, count, flagsOut.data());
    env->SetIntArrayRegion(headings, 0, count, headingsOut.data());
    env->SetIntArrayRegion(indents, 0, count, indentsOut.data());
    jclass resultClass = JStreamTextMarkdown::javaClassStatic().get();
    jmethodID constructor = env->GetMethodID(resultClass, "<init>", "(Ljava/lang/String;[I[I[I[I[I[Ljava/lang/String;)V");
    jobject result = env->NewObject(resultClass, constructor, text, starts, lengths, flags, headings, indents, urls);
    return jni::adopt_local(reinterpret_cast<JStreamTextMarkdown::javaobject>(result));
  }

  static void registerNatives() {
    javaClassStatic()->registerNatives({
        makeNativeMethod("takeBuffer", JStreamTextNative::takeBuffer),
        makeNativeMethod("streamSource", JStreamTextNative::streamSource),
        makeNativeMethod("listenStream", JStreamTextNative::listenStream),
        makeNativeMethod("unlistenStream", JStreamTextNative::unlistenStream),
        makeNativeMethod("stateSource", JStreamTextNative::stateSource),
        makeNativeMethod("stateGeneration", JStreamTextNative::stateGeneration),
        makeNativeMethod("stateSourceOwner", JStreamTextNative::stateSourceOwner),
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
        makeNativeMethod("parseMarkdown", JStreamTextNative::parseMarkdown),
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
    facebook::react::JStreamTextMarkdown::javaClassStatic();
  });
}
