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
#include <limits>
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
struct JStreamTextMarkdownCell : jni::JavaClass<JStreamTextMarkdownCell> {
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextMarkdown$RichCell;";
};
struct JStreamTextMarkdownBlock : jni::JavaClass<JStreamTextMarkdownBlock> {
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextMarkdown$RichBlock;";
};

struct MarkdownJniMetadata {
  jclass stringClass, resultClass, cellClass, blockClass;
  jmethodID resultConstructor, cellConstructor, blockConstructor;
};

const MarkdownJniMetadata &markdownJniMetadata() {
  // javaClassStatic retains global references. Resolve constructors once while
  // JNI_OnLoad has the app class loader, before any prepare/parser worker runs.
  static const MarkdownJniMetadata metadata = [] {
    JNIEnv *env = jni::Environment::current();
    const auto constructor = [env](jclass type, const char *signature) {
      const auto method = env->GetMethodID(type, "<init>", signature);
      if (!method) jni::throwPendingJniExceptionAsCppException();
      return method;
    };
    MarkdownJniMetadata value{};
    value.stringClass = jni::JString::javaClassStatic().get();
    value.resultClass = JStreamTextMarkdown::javaClassStatic().get();
    value.cellClass = JStreamTextMarkdownCell::javaClassStatic().get();
    value.blockClass = JStreamTextMarkdownBlock::javaClassStatic().get();
    value.resultConstructor = constructor(value.resultClass, "(Ljava/lang/String;[I[I[I[I[I[Ljava/lang/String;[Lcom/textstream/StreamTextMarkdown$RichBlock;)V");
    value.cellConstructor = constructor(value.cellClass, "(Ljava/lang/String;[I[I[I[I[I[Ljava/lang/String;ZI)V");
    value.blockConstructor = constructor(value.blockClass, "(IIII[Lcom/textstream/StreamTextMarkdown$RichCell;Ljava/lang/String;Ljava/lang/String;)V");
    return value;
  }();
  return metadata;
}

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

  /** MD4C output materialized as primitive arrays on Markdown or prepare workers. */
  static jni::local_ref<JStreamTextMarkdown> parseMarkdownStreaming(
      jni::alias_ref<jclass>,
      jni::alias_ref<jstring> markdown,
      jboolean github,
      jboolean final) {
    if (!markdown) return nullptr;
    const auto input = markdown->toStdString();
    // Worker-local vectors avoid reallocating parser state for every coalesced update.
    // Prepare workers also use this entry point; thread teardown releases their buffers.
    thread_local MarkdownParserHolder holder;
    STMarkdownParser *parser = holder.parser;
    if (!parser) {
      parser = holder.parser = stmd_create();
      // Kotlin owns the per-view diff. This worker can serve different views and
      // never consumes the C++ changedStartUTF16/history snapshot.
      if (parser) stmd_set_track_changes(parser, false);
    }
    if (!parser) return nullptr;
    const STMarkdownDocument *document = stmd_parse_decoded_streaming(
        parser, input.data(), input.size(), github == JNI_TRUE, final == JNI_TRUE);
    if (!document || document->textLength > static_cast<size_t>(INT32_MAX) ||
        document->runCount > static_cast<size_t>(INT32_MAX) ||
        document->blockCount > static_cast<size_t>(INT32_MAX)) {
      return nullptr;
    }
    if (document->error != 0) return nullptr;
    JNIEnv *env = jni::Environment::current();
    // JNI could propagate a pending exception to Java, but this method instead
    // returns null because Kotlin deliberately falls back to plain text for a
    // snapshot it cannot materialize. Clear it before making another JNI call.
    const auto allocationFailed = [env](jobject value) {
      if (value && !env->ExceptionCheck()) return false;
      if (env->ExceptionCheck()) env->ExceptionClear();
      return true;
    };
    const auto callFailed = [env] {
      if (!env->ExceptionCheck()) return false;
      env->ExceptionClear();
      return true;
    };
    const auto fitsJint = [](size_t value) {
      return value <= static_cast<size_t>(INT32_MAX);
    };
    if (!document->text || !fitsJint(document->textLength) ||
        (document->runCount && !document->runs) ||
        (document->blockCount && !document->blocks)) {
      return nullptr;
    }
    for (size_t i = 0; i < document->runCount; ++i) {
      const auto &run = document->runs[i];
      if (!fitsJint(run.start) || !fitsJint(run.length) ||
          run.start > document->textLength || run.length > document->textLength - run.start) {
        return nullptr;
      }
    }
    for (size_t bi = 0; bi < document->blockCount; ++bi) {
      const auto &block = document->blocks[bi];
      if (!fitsJint(block.start) || !fitsJint(block.rowCount) || !fitsJint(block.columnCount) ||
          !fitsJint(block.altLength)) {
        return nullptr;
      }
      if (block.kind != STMD_BLOCK_TABLE) continue;
      if (block.columnCount && block.rowCount > std::numeric_limits<size_t>::max() / block.columnCount) {
        return nullptr;
      }
      const size_t cellCount = block.rowCount * block.columnCount;
      if (!fitsJint(cellCount) || (cellCount && !block.cells)) return nullptr;
      for (size_t ci = 0; ci < cellCount; ++ci) {
        const auto &cell = block.cells[ci];
        if (!fitsJint(cell.textLength) || !fitsJint(cell.runCount) ||
            (cell.textLength && !cell.text) || (cell.runCount && !cell.runs)) {
          return nullptr;
        }
        for (size_t ri = 0; ri < cell.runCount; ++ri) {
          const auto &run = cell.runs[ri];
          if (!fitsJint(run.start) || !fitsJint(run.length) ||
              run.start > cell.textLength || run.length > cell.textLength - run.start) {
            return nullptr;
          }
        }
      }
    }
    const auto &metadata = markdownJniMetadata();
    const jsize count = static_cast<jsize>(document->runCount);
    jstring text = env->NewString(reinterpret_cast<const jchar *>(document->text), static_cast<jsize>(document->textLength));
    if (allocationFailed(text)) return nullptr;
    jintArray starts = env->NewIntArray(count);
    if (allocationFailed(starts)) return nullptr;
    jintArray lengths = env->NewIntArray(count);
    if (allocationFailed(lengths)) return nullptr;
    jintArray flags = env->NewIntArray(count);
    if (allocationFailed(flags)) return nullptr;
    jintArray headings = env->NewIntArray(count);
    if (allocationFailed(headings)) return nullptr;
    jintArray indents = env->NewIntArray(count);
    if (allocationFailed(indents)) return nullptr;
    jclass stringClass = metadata.stringClass;
    jobjectArray urls = env->NewObjectArray(count, stringClass, nullptr);
    if (allocationFailed(urls)) return nullptr;
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
        if (allocationFailed(url)) return nullptr;
        env->SetObjectArrayElement(urls, i, url);
        if (callFailed()) return nullptr;
        env->DeleteLocalRef(url);
      }
    }
    env->SetIntArrayRegion(starts, 0, count, startsOut.data());
    if (callFailed()) return nullptr;
    env->SetIntArrayRegion(lengths, 0, count, lengthsOut.data());
    if (callFailed()) return nullptr;
    env->SetIntArrayRegion(flags, 0, count, flagsOut.data());
    if (callFailed()) return nullptr;
    env->SetIntArrayRegion(headings, 0, count, headingsOut.data());
    if (callFailed()) return nullptr;
    env->SetIntArrayRegion(indents, 0, count, indentsOut.data());
    if (callFailed()) return nullptr;
    jclass resultClass = metadata.resultClass;
    jclass cellClass = metadata.cellClass;
    jclass blockClass = metadata.blockClass;
    jmethodID cellConstructor = metadata.cellConstructor;
    jmethodID blockConstructor = metadata.blockConstructor;
    jobjectArray blocks = env->NewObjectArray(static_cast<jsize>(document->blockCount), blockClass, nullptr);
    if (allocationFailed(blocks)) return nullptr;
    for (jsize bi = 0; bi < static_cast<jsize>(document->blockCount); ++bi) {
      const auto &block = document->blocks[bi];
      jobjectArray cells = nullptr;
      const size_t cellCount = block.rowCount * block.columnCount;
      if (block.kind == STMD_BLOCK_TABLE) {
        cells = env->NewObjectArray(static_cast<jsize>(cellCount), cellClass, nullptr);
        if (allocationFailed(cells)) return nullptr;
        for (jsize ci = 0; ci < static_cast<jsize>(cellCount); ++ci) {
          const auto &cell = block.cells[ci]; const jsize runs = static_cast<jsize>(cell.runCount);
          jstring cellText = env->NewString(reinterpret_cast<const jchar *>(cell.text), static_cast<jsize>(cell.textLength));
          if (allocationFailed(cellText)) return nullptr;
          jintArray cs = env->NewIntArray(runs);
          if (allocationFailed(cs)) return nullptr;
          jintArray cl = env->NewIntArray(runs);
          if (allocationFailed(cl)) return nullptr;
          jintArray cf = env->NewIntArray(runs);
          if (allocationFailed(cf)) return nullptr;
          jintArray ch = env->NewIntArray(runs);
          if (allocationFailed(ch)) return nullptr;
          jintArray ciIndent = env->NewIntArray(runs);
          if (allocationFailed(ciIndent)) return nullptr;
          jobjectArray cu = env->NewObjectArray(runs, stringClass, nullptr);
          if (allocationFailed(cu)) return nullptr;
          std::vector<jint> vs(runs), vl(runs), vf(runs), vh(runs), vi(runs);
          for (jsize ri = 0; ri < runs; ++ri) { const auto &r = cell.runs[ri]; vs[ri]=r.start; vl[ri]=r.length; vf[ri]=r.flags; vh[ri]=r.headingLevel; vi[ri]=r.indent; if (r.url) { jstring u=utf8String(env,r.url); if (allocationFailed(u)) return nullptr; env->SetObjectArrayElement(cu,ri,u); if (callFailed()) return nullptr; env->DeleteLocalRef(u); } }
          if (runs) { env->SetIntArrayRegion(cs,0,runs,vs.data()); if (callFailed()) return nullptr; env->SetIntArrayRegion(cl,0,runs,vl.data()); if (callFailed()) return nullptr; env->SetIntArrayRegion(cf,0,runs,vf.data()); if (callFailed()) return nullptr; env->SetIntArrayRegion(ch,0,runs,vh.data()); if (callFailed()) return nullptr; env->SetIntArrayRegion(ciIndent,0,runs,vi.data()); if (callFailed()) return nullptr; }
          jobject cellObject = env->NewObject(cellClass, cellConstructor, cellText, cs, cl, cf, ch, ciIndent, cu, (cell.flags & STMD_BLOCK_CELL_HEADER) ? JNI_TRUE : JNI_FALSE, static_cast<jint>(cell.alignment));
          if (allocationFailed(cellObject)) return nullptr;
          env->SetObjectArrayElement(cells, ci, cellObject);
          if (callFailed()) return nullptr;
          env->DeleteLocalRef(cellObject);
          env->DeleteLocalRef(cellText);
          env->DeleteLocalRef(cs); env->DeleteLocalRef(cl); env->DeleteLocalRef(cf);
          env->DeleteLocalRef(ch); env->DeleteLocalRef(ciIndent); env->DeleteLocalRef(cu);
        }
      }
      jstring imageUrl = block.url ? utf8String(env, block.url) : nullptr;
      if (block.url && allocationFailed(imageUrl)) return nullptr;
      jstring alt = block.alt ? env->NewString(reinterpret_cast<const jchar *>(block.alt), static_cast<jsize>(block.altLength)) : nullptr;
      if (block.alt && allocationFailed(alt)) return nullptr;
      jobject rich = env->NewObject(blockClass, blockConstructor, static_cast<jint>(block.kind), static_cast<jint>(block.start), static_cast<jint>(block.rowCount), static_cast<jint>(block.columnCount), cells, imageUrl, alt);
      if (allocationFailed(rich)) return nullptr;
      env->SetObjectArrayElement(blocks, bi, rich); env->DeleteLocalRef(rich);
      if (callFailed()) return nullptr;
      if (cells) env->DeleteLocalRef(cells); if (imageUrl) env->DeleteLocalRef(imageUrl); if (alt) env->DeleteLocalRef(alt);
    }
    jmethodID constructor = metadata.resultConstructor;
    jobject result = env->NewObject(resultClass, constructor, text, starts, lengths, flags, headings, indents, urls, blocks);
    if (allocationFailed(result)) return nullptr;
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
        makeNativeMethod("parseMarkdownStreaming", JStreamTextNative::parseMarkdownStreaming),
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
    facebook::react::markdownJniMetadata();
  });
}
