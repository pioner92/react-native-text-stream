// Layouts of finished `source` buffers, prepared in the shadow node's measure (off the main
// thread) and carried to the component view in the state. Objective-C++ only.
#pragma once

#import <UIKit/UIKit.h>

#import "TextStream-Swift-Cxx-Umbrella.hpp"
#include "../../cpp/StreamTextViewShadowNode.hpp"

NS_ASSUME_NONNULL_BEGIN

StreamTextStyle *StreamTextStyleFromProps(const facebook::react::StreamTextViewProps &props);

/** The layout in `prepared` if it was made for `source`. Any thread. */
StreamTextPreparedLayout *_Nullable StreamTextPreparedLayoutOf(
    const facebook::react::StreamTextPreparedRef &prepared,
    const std::shared_ptr<facebook::jsi::MutableBuffer> &source);

/**
 * Lays out these props' finished `source` at `width` on a background queue, so the next mount of
 * the same text (a list scrolling back to it, a chat reopened) finds it ready. Any thread.
 */
void StreamTextPrepareInBackground(std::shared_ptr<const facebook::react::StreamTextViewProps> props, CGFloat width);

/** Tests: forget every cached layout; how many there are. Any thread. */
void StreamTextPreparedCacheClear(void);
NSInteger StreamTextPreparedCacheCount(void);

NS_ASSUME_NONNULL_END
