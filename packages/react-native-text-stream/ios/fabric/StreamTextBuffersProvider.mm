// Registers the C++ StreamTextBuffers TurboModule: package.json's codegenConfig.ios.modulesProvider
// maps the module name to this class, so apps need no AppDelegate changes.

#import <Foundation/Foundation.h>
#import <ReactCommon/RCTTurboModule.h>

#include "../../cpp/StreamTextBuffers.hpp"

@interface StreamTextBuffersProvider : NSObject <RCTModuleProvider>
@end

@implementation StreamTextBuffersProvider

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  return std::make_shared<facebook::react::StreamTextBuffersModule>(params.jsInvoker);
}

@end
