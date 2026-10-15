// streamFetch(): an HTTP request whose response body goes straight into a TSB1 buffer on the
// network thread, with no JavaScript per chunk. The request runs on the platform's own client
// (StreamTextFetch.mm: URLSession; StreamTextFetchAndroid.cpp + StreamTextFetch.kt: OkHttp).
#pragma once

#include <jsi/jsi.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "streaming/StreamTextResponseParser.hpp"

namespace facebook::react {

using StreamTextHeaders = std::vector<std::pair<std::string, std::string>>;

// Both platforms and the Android view must be able to address the whole header + body.
inline constexpr uint64_t kStreamTextMaxCapacity = 0x7fffffffULL - 64;
inline constexpr double kStreamTextMaxTimeoutMs = 0x7fffffff;

/**
 * A TSB1 buffer in its own anonymous mapping: pages are only committed once they are written.
 * Its writer (streamFetch) tells its observers about every write, so a view showing it is woken
 * instead of polling the header.
 */
class StreamTextMappedBuffer : public jsi::MutableBuffer {
 public:
  using Observer = std::function<void()>;

  /** An initialized, open buffer of `capacity` data bytes; nullptr if the mapping fails. */
  static std::shared_ptr<StreamTextMappedBuffer> create(uint64_t capacity);
  ~StreamTextMappedBuffer() override;

  size_t size() const override {
    return size_;
  }
  uint8_t *data() override {
    return base_;
  }

  /**
   * `observer` runs on the writer's thread after new bytes or the final status: it must not block
   * (post to the main thread). It may still run once after `unobserve` returns.
   */
  uint64_t observe(Observer observer);
  void unobserve(uint64_t token);
  /** The writer: bytes were published, or the status is final. */
  void notify();
  /** Publishes a terminal status, wakes readers, and enforces the finished-stream cache limits. */
  void finish(uint32_t status);

  /** Unique for the process's life (an address may be reused once a buffer is freed). */
  uint64_t serial() const {
    return serial_;
  }

  StreamTextMappedBuffer(uint8_t *base, size_t size);

 private:
  uint64_t serial_;
  uint8_t *base_;
  size_t size_;
  std::mutex mutex_;
  uint64_t lastToken_{0};
  std::vector<std::pair<uint64_t, std::shared_ptr<Observer>>> observers_;
};

struct StreamTextFetchRequest {
  std::string url;
  std::string method;
  StreamTextHeaders headers;
  std::optional<std::string> body;
  /** Longest wait for the next bytes, in seconds; 0: the platform's default. */
  double timeoutSeconds{0};
  StreamTextParserOptions parser;
};

/**
 * The request's progress, on a network thread and in order: `onResponse` at most once, then
 * `onComplete` exactly once, last. The buffer's status is final before `onComplete`.
 */
class StreamTextFetchListener {
 public:
  virtual ~StreamTextFetchListener() = default;
  virtual void onResponse(int status, StreamTextHeaders headers, std::string url) = 0;
  /** `error`: empty unless the request failed. */
  virtual void onComplete(std::string error) = 0;
};

class StreamTextFetchTask {
 public:
  virtual ~StreamTextFetchTask() = default;
  /** Any thread, any number of times: the request ends as cancelled unless it already ended. */
  virtual void cancel() = 0;
};

/**
 * Platform: starts the request. The body is appended to `buffer` (its only writer); its status
 * becomes done, error, cancelled, or overflow (the request is then cancelled).
 */
std::shared_ptr<StreamTextFetchTask> streamTextStartFetch(
    StreamTextFetchRequest request,
    std::shared_ptr<StreamTextMappedBuffer> buffer,
    std::shared_ptr<StreamTextFetchListener> listener);

} // namespace facebook::react
