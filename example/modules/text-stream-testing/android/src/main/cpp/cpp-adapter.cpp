#include "TextStreamTestingOnLoad.hpp"
#include "TextStreamBuffer.h"
#include <fbjni/ByteBuffer.h>
#include <fbjni/fbjni.h>
#include <jni.h>

namespace {

using namespace facebook;

/** `TsbWriter` (Kotlin): writes TSB1 buffers the way nitro-fetch does. */
struct JTsbWriter : jni::JavaClass<JTsbWriter> {
  static constexpr auto kJavaDescriptor = "Lcom/margelo/nitro/textstreamtesting/TsbWriter;";

  static jlong address(jni::alias_ref<jclass>, jni::alias_ref<jni::JByteBuffer> buffer) {
    return reinterpret_cast<jlong>(buffer->getDirectAddress());
  }

  static void init(jni::alias_ref<jclass>, jlong base, jlong capacity) {
    tsb_init(reinterpret_cast<void *>(base), static_cast<uint64_t>(capacity));
  }

  static jboolean append(jni::alias_ref<jclass>, jlong base, jni::alias_ref<jni::JArrayByte> bytes, jint offset, jint length) {
    auto pinned = bytes->pin();
    return tsb_append(reinterpret_cast<void *>(base), pinned.get() + offset, static_cast<uint64_t>(length)) != 0;
  }

  static void setStatus(jni::alias_ref<jclass>, jlong base, jint status) {
    tsb_set_status(reinterpret_cast<void *>(base), static_cast<uint32_t>(status));
  }

  static void registerNatives() {
    javaClassStatic()->registerNatives({
        makeNativeMethod("address", JTsbWriter::address),
        makeNativeMethod("init", JTsbWriter::init),
        makeNativeMethod("append", JTsbWriter::append),
        makeNativeMethod("setStatus", JTsbWriter::setStatus),
    });
  }
};

} // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  return facebook::jni::initialize(vm, [] {
    margelo::nitro::textstreamtesting::registerAllNatives();
    JTsbWriter::registerNatives();
  });
}
