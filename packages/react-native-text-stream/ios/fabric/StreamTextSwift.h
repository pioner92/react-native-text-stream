// The module's Swift classes (StreamTextRenderer, StreamTextPreparedLayout, ...) for Objective-C++.
#pragma once

#if __has_include("TextStream-Swift.h")
#import "TextStream-Swift.h"
#elif __has_include(<TextStream/TextStream-Swift.h>)
#import <TextStream/TextStream-Swift.h>
#else
#error "TextStream-Swift.h not found: the Swift module's name (= podspec name) must be TextStream"
#endif
