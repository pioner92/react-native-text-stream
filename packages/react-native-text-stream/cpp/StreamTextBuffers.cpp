#include "StreamTextBuffers.hpp"

namespace facebook::react {

StreamTextBufferRegistry &StreamTextBufferRegistry::shared() {
  static StreamTextBufferRegistry registry;
  return registry;
}

int StreamTextBufferRegistry::put(std::shared_ptr<jsi::MutableBuffer> buffer) {
  std::lock_guard lock(mutex_);
  // A Fabric command can be discarded during surface teardown. Once its JS owner disappears, the
  // weak entry has no payload and can be pruned on the next handoff; no elapsed-time policy is used.
  std::erase_if(buffers_, [](const auto &entry) { return entry.second.expired(); });
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
  auto buffer = it->second.lock();
  buffers_.erase(it);
  return buffer;
}

#ifdef STREAMTEXT_TESTING
std::pair<size_t, size_t> StreamTextBufferRegistry::stats() {
  std::lock_guard lock(mutex_);
  std::erase_if(buffers_, [](const auto &entry) { return entry.second.expired(); });
  size_t bytes = 0;
  for (const auto &[id, weakBuffer] : buffers_) {
    if (auto buffer = weakBuffer.lock()) bytes += buffer->size();
  }
  return {buffers_.size(), bytes};
}
#endif

#ifndef STREAMTEXT_NATIVE_TESTING
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
#endif // STREAMTEXT_NATIVE_TESTING

} // namespace facebook::react
