// streamFetch(url, { streamId }) and <StreamText streamId={…} />: the request and the view find each
// other by an id the app chooses (a message's id), in this registry, in any order: no buffer or
// object passes through JS.
#pragma once

#include "StreamTextFetch.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace facebook::react {

class StreamTextStreams {
 public:
  using Listener = std::function<void()>;

  static StreamTextStreams &shared();

  /** The buffer streamed under `id` now (a new request replaces it); listeners are told. */
  void publish(const std::string &id, std::shared_ptr<StreamTextMappedBuffer> buffer);
  /** The buffer streamed under `id`, or nullptr. Any thread. */
  std::shared_ptr<StreamTextMappedBuffer> find(const std::string &id);
  /**
   * `listener` runs, on any thread, whenever `id` gets a buffer (published or replaced): post to the
   * main thread. A listened-to stream is kept.
   */
  uint64_t listen(const std::string &id, Listener listener);
  void unlisten(const std::string &id, uint64_t token);
  /** Forgets `id`: a view showing it keeps what it shows. */
  void release(const std::string &id);
  /** Forgets every stream (the JS runtime is going away). */
  void clear();

  /** Finished streams no view shows that are kept for a remount; older ones are forgotten. */
  static constexpr size_t kKeptFinished = 64;

 private:
  struct Entry {
    std::shared_ptr<StreamTextMappedBuffer> buffer;
    std::vector<std::pair<uint64_t, std::shared_ptr<Listener>>> listeners;
    uint64_t lastUse{0};
  };

  void evictLocked();

  std::mutex mutex_;
  uint64_t clock_{0};
  uint64_t lastToken_{0};
  std::unordered_map<std::string, Entry> entries_;
};

} // namespace facebook::react
