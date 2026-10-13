// Hands an ArrayBuffer from JS to a StreamTextView without a React render. Fabric view commands
// carry only folly::dynamic arguments, which lose an ArrayBuffer, so JS first puts the buffer here
// through the StreamTextBuffers TurboModule and then sends the view an `attach` command with its id.
#pragma once

#ifndef STREAMTEXT_NATIVE_TESTING
#include <ReactCommon/TurboModule.h>
#endif
#include <jsi/jsi.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>
#include <utility>

namespace facebook::react {

/**
 * Buffers waiting for their `attach` command; each id is taken once. The registry deliberately
 * keeps a weak reference: JS already retains the buffer as the component's current/pending source.
 * If Fabric drops a command while tearing down a surface, that JS owner can go away and the global
 * handoff cannot pin the buffer forever. Defined out of line (StreamTextBuffers.cpp): on Android
 * the module and the view live in different libraries, which must share one registry.
 */
class StreamTextBufferRegistry {
 public:
  static StreamTextBufferRegistry &shared();

  int put(std::shared_ptr<jsi::MutableBuffer> buffer);
  std::shared_ptr<jsi::MutableBuffer> take(int id);

#ifdef STREAMTEXT_TESTING
  /** Test builds only: live JS-owned handoffs still awaiting a Fabric `attach` and their bytes. */
  std::pair<size_t, size_t> stats();
#endif

 private:
  std::mutex mutex_;
  int lastId_{0};
  std::unordered_map<int, std::weak_ptr<jsi::MutableBuffer>> buffers_;
};

#ifndef STREAMTEXT_NATIVE_TESTING
class StreamTextFetchTasks;

/**
 * - `put(buffer: ArrayBuffer): number`: keeps a natively-backed buffer for an `attach` command and
 *   returns its id, or 0 for a buffer created in JS (it may only be touched on the JS thread).
 * - `fetch(url, init, onEvent): { id, buffer }` and `cancel(id)`: streamFetch() (StreamTextFetch.hpp);
 *   `abortStream(streamId)` and `releaseStream(streamId)` for one under a streamId (StreamTextStreams.hpp).
 */
class StreamTextBuffersModule : public TurboModule {
 public:
  static constexpr auto kModuleName = "StreamTextBuffers";

  explicit StreamTextBuffersModule(std::shared_ptr<CallInvoker> jsInvoker);
  ~StreamTextBuffersModule() override;

 private:
  static jsi::Value put(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value fetch(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value cancel(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value abortStream(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value releaseStream(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value cancelledBuffer(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value readBuffer(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value streamBuffer(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value hasStream(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value watchStream(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value unwatchStream(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
#ifdef STREAMTEXT_TESTING
  static jsi::Value registryStats(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
  static jsi::Value collectGarbageForTesting(jsi::Runtime &runtime, TurboModule &module, const jsi::Value *args, size_t count);
#endif

  std::shared_ptr<StreamTextFetchTasks> fetches_;
  uint64_t owner_;
  struct Watch { std::string id; std::shared_ptr<std::atomic<bool>> active; };
  std::unordered_map<uint64_t, Watch> watches_;
};
#endif // STREAMTEXT_NATIVE_TESTING

} // namespace facebook::react
