// Android prepared layouts (StreamTextPreparedAndroid.cpp), for the JNI in textstream.cpp.
#pragma once

#include "StreamTextViewShadowNode.hpp"

#include <fbjni/fbjni.h>
#include <cstdint>
#include <string>

namespace facebook::react {

/** `com.textstream.StreamTextPreparedLayout`. */
struct JStreamTextPreparedLayout : jni::JavaClass<JStreamTextPreparedLayout> {
  static constexpr auto kJavaDescriptor = "Lcom/textstream/StreamTextPreparedLayout;";
};

/** The layout of `prepared` if it was made from `source`'s current text, else null. */
jni::local_ref<JStreamTextPreparedLayout::javaobject> streamTextPreparedLayoutOf(
    const StreamTextPreparedRef &prepared,
    const std::shared_ptr<jsi::MutableBuffer> &source);

/**
 * Lays a finished `source` out in the background for its next mount: a view leaving that showed it
 * in its own layout (it streamed it), with these style props at `width` dp.
 */
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
    int32_t color);

/** Tests and benchmarks. Off: finished sources are laid out by the view, as streams are. */
void streamTextPreparedSetEnabled(bool enabled);
/** Forgets every layout (a memory warning, or a cold start for a benchmark). */
void streamTextPreparedCacheClear(bool invalidate);
int streamTextPreparedCacheCount();
std::string streamTextPreparedCacheDump();

} // namespace facebook::react
