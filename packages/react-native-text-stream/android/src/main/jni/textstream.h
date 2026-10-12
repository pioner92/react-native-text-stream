// Included by the app's autolinking (libraryName "textstream" in react-native.config.js).
#pragma once

#include <ReactCommon/JavaTurboModule.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

namespace facebook::react {

/** No Java TurboModules: StreamTextBuffers is C++ (StreamTextBuffersModule.h). */
JSI_EXPORT
std::shared_ptr<TurboModule> textstream_ModuleProvider(
    const std::string &moduleName,
    const JavaTurboModule::InitParams &params);

} // namespace facebook::react
