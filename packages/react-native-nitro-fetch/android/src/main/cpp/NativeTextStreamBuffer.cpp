// TSB1 buffers for `enableNativeBuffer` (layout and atomics in ios/NitroTextStreamBuffer.h, shared
// with iOS): Kotlin has no acquire/release stores on raw memory, so the writes go through here.
#include "NativeTextStreamBuffer.hpp"

#include "../../../../ios/NitroTextStreamBuffer.h"
#include <NitroModules/ArrayBuffer.hpp>
#include <NitroModules/JArrayBuffer.hpp>
#include <fbjni/ByteBuffer.h>
#include <fbjni/fbjni.h>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <sys/mman.h>

namespace margelo::nitro::nitrofetch {

using namespace facebook;

namespace {

struct JNativeTextStreamBuffer : jni::JavaClass<JNativeTextStreamBuffer> {
  static constexpr auto kJavaDescriptor = "Lcom/margelo/nitro/nitrofetch/NativeTextStreamBufferJni;";

  /** Anonymous mmap pages are only committed once written. */
  static jni::local_ref<JArrayBuffer::javaobject> create(jni::alias_ref<jclass>, jlong capacity) {
    size_t size = TSB_HEADER_SIZE + static_cast<size_t>(capacity);
    void* base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
      throw std::runtime_error("mmap of " + std::to_string(size) + " bytes failed (errno " + std::to_string(errno) + ")");
    }
    tsb_init(base, static_cast<uint64_t>(capacity));
    auto buffer = ArrayBuffer::wrap(static_cast<uint8_t*>(base), size, [base, size] { munmap(base, size); });
    return JArrayBuffer::wrap(buffer);
  }

  static jlong address(jni::alias_ref<jclass>, jni::alias_ref<JArrayBuffer::javaobject> buffer) {
    return reinterpret_cast<jlong>(buffer->cthis()->getArrayBuffer()->data());
  }

  static jboolean append(jni::alias_ref<jclass>, jlong base, jni::alias_ref<jni::JByteBuffer> src, jint length) {
    return tsb_append(reinterpret_cast<void*>(base), src->getDirectBytes(), static_cast<uint64_t>(length)) != 0;
  }

  static void setStatus(jni::alias_ref<jclass>, jlong base, jint status) {
    tsb_set_status(reinterpret_cast<void*>(base), static_cast<uint32_t>(status));
  }
};

} // namespace

void registerNativeTextStreamBuffer() {
  JNativeTextStreamBuffer::javaClassStatic()->registerNatives({
      makeNativeMethod("nativeCreate", JNativeTextStreamBuffer::create),
      makeNativeMethod("nativeAddress", JNativeTextStreamBuffer::address),
      makeNativeMethod("nativeAppend", JNativeTextStreamBuffer::append),
      makeNativeMethod("nativeSetStatus", JNativeTextStreamBuffer::setStatus),
  });
}

} // namespace margelo::nitro::nitrofetch
