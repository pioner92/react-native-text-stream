#include "StreamTextBuffers.hpp"

namespace facebook::react {

StreamTextBufferRegistry &StreamTextBufferRegistry::shared() {
  static StreamTextBufferRegistry registry;
  return registry;
}

int StreamTextBufferRegistry::put(std::shared_ptr<jsi::MutableBuffer> buffer) {
  std::lock_guard lock(mutex_);
  int id = ++lastId_;
  buffers_[id] = std::move(buffer);
  return id;
}

std::shared_ptr<jsi::MutableBuffer> StreamTextBufferRegistry::take(int id) {
  std::lock_guard lock(mutex_);
  auto it = buffers_.find(id);
  if (it == buffers_.end()) {
    return nullptr;
  }
  auto buffer = std::move(it->second);
  buffers_.erase(it);
  return buffer;
}

jsi::Value StreamTextBuffersModule::put(
    jsi::Runtime &runtime,
    TurboModule & /*module*/,
    const jsi::Value *args,
    size_t count) {
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

} // namespace facebook::react
