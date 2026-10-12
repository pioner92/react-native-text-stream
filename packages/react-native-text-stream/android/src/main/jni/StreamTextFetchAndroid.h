#pragma once

#include <fbjni/fbjni.h>

namespace facebook::react {

/** `com.textstream.StreamTextFetch`: OkHttp writes a response body into a TSB1 buffer. */
struct JStreamTextFetch : jni::JavaClass<JStreamTextFetch> {
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextFetch;";

  static void nativeResponse(
      jni::alias_ref<jclass>,
      jlong handle,
      jint status,
      jni::alias_ref<jni::JArrayClass<jstring>> headers,
      jni::alias_ref<jstring> url);
  static void nativePublish(jni::alias_ref<jclass>, jlong handle, jlong committed);
  static void nativeComplete(jni::alias_ref<jclass>, jlong handle, jint status, jni::alias_ref<jstring> error);

  static void registerNatives();
};

} // namespace facebook::react
