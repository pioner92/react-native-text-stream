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

/** Buffers waiting for their `attach` command; each id is taken once. */
class StreamTextBufferRegistry {
 public:
  static StreamTextBufferRegistry &shared() {
    static StreamTextBufferRegistry registry;
    return registry;
  }

  int put(std::shared_ptr<jsi::MutableBuffer> buffer) {
    std::lock_guard lock(mutex_);
    int id = ++lastId_;
    buffers_[id] = std::move(buffer);
    return id;
  }

  std::shared_ptr<jsi::MutableBuffer> take(int id) {
    std::lock_guard lock(mutex_);
    auto it = buffers_.find(id);
    if (it == buffers_.end()) {
      return nullptr;
    }
    auto buffer = std::move(it->second);
    buffers_.erase(it);
    return buffer;
  }

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
  explicit StreamTextBuffersModule(std::shared_ptr<CallInvoker> jsInvoker)
      : TurboModule("StreamTextBuffers", std::move(jsInvoker)) {
    methodMap_["put"] = MethodMetadata{1, put};
  }

 private:
  static jsi::Value put(jsi::Runtime &runtime, TurboModule & /*module*/, const jsi::Value *args, size_t count) {
    if (count < 1 || !args[0].isObject()) {
      return 0;
    }
    auto object = args[0].getObject(runtime);
    if (!object.isArrayBuffer(runtime)) {
      return 0;
    }
    auto buffer = object.getArrayBuffer(runtime).tryGetMutableBuffer(runtime);
    return buffer ? StreamTextBufferRegistry::shared().put(std::move(buffer)) : 0;
  }
};

} // namespace facebook::react
