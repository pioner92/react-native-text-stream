// Hands an ArrayBuffer from JS to a StreamTextView without a React render. Fabric view commands
// carry only folly::dynamic arguments, which lose an ArrayBuffer, so JS first puts the buffer here
// through the StreamTextBuffers TurboModule and then sends the view an `attach` command with its id.
#pragma once

#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace facebook::react {

/**
 * Buffers waiting for their `attach` command; each id is taken once. Defined out of line
 * (StreamTextBuffers.cpp): on Android the module and the view live in different libraries, which
 * must share one registry.
 */
class StreamTextBufferRegistry {
 public:
  static StreamTextBufferRegistry &shared();

  int put(std::shared_ptr<jsi::MutableBuffer> buffer);
  std::shared_ptr<jsi::MutableBuffer> take(int id);

 private:
  std::mutex mutex_;
  int lastId_{0};
  std::unordered_map<int, std::shared_ptr<jsi::MutableBuffer>> buffers_;
};

/**
 * `put(buffer: ArrayBuffer): number`: keeps a natively-backed buffer for an `attach` command and
 * returns its id, or 0 for a buffer created in JS (it may only be touched on the JS thread).
 */
class StreamTextBuffersModule : public TurboModule {
 public:
  static constexpr auto kModuleName = "StreamTextBuffers";

  explicit StreamTextBuffersModule(std::shared_ptr<CallInvoker> jsInvoker)
      : TurboModule(kModuleName, std::move(jsInvoker)) {
    methodMap_["put"] = MethodMetadata{1, put};
  }

 private:
  static jsi::Value put(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
};

} // namespace facebook::react
