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
  void publish(const std::string &id, std::shared_ptr<StreamTextMappedBuffer> buffer,
               std::string requestKey = {}, uint64_t owner = 0);
  /** The buffer streamed under `id`, or nullptr. Any thread. */
  std::shared_ptr<StreamTextMappedBuffer> find(const std::string &id);
  /** `includeEvicted`: also true when that request's stream was evicted, not explicitly released. */
  bool matches(const std::string &id, const std::string &requestKey, bool includeEvicted = false);
  void sweep();
  /** A runtime only releases the streams it published, never another runtime's streams. */
  void releaseOwner(uint64_t owner);
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
  static constexpr uint64_t kKeptBytes = 64 * 1024 * 1024;
  /** Hashes of evicted streams' request keys, so a remount doesn't silently resend a POST. */
  static constexpr size_t kKeptEvictedKeys = 4096;

 private:
  struct Entry {
    std::shared_ptr<StreamTextMappedBuffer> buffer;
    std::vector<std::pair<uint64_t, std::shared_ptr<Listener>>> listeners;
    uint64_t lastUse{0};
    uint64_t owner{0};
    std::string requestKey;
  };
  struct Evicted {
    size_t keyHash{0};
    uint64_t owner{0};
    uint64_t lastUse{0};
  };

  void evictLocked();

  std::mutex mutex_;
  uint64_t clock_{0};
  uint64_t lastToken_{0};
  std::unordered_map<std::string, Entry> entries_;
  std::unordered_map<std::string, Evicted> evicted_;
};

} // namespace facebook::react
