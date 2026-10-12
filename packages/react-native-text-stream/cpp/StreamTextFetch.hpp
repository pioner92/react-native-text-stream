// streamFetch(): an HTTP request whose response body goes straight into a TSB1 buffer on the
// network thread, with no JavaScript per chunk. The request runs on the platform's own client
// (StreamTextFetch.mm: URLSession; StreamTextFetchAndroid.cpp + StreamTextFetch.kt: OkHttp).
#pragma once

#include <jsi/jsi.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facebook::react {

using StreamTextHeaders = std::vector<std::pair<std::string, std::string>>;

/** A TSB1 buffer in its own anonymous mapping: pages are only committed once they are written. */
class StreamTextMappedBuffer : public jsi::MutableBuffer {
 public:
  /** An initialized, open buffer of `capacity` data bytes; nullptr if the mapping fails. */
  static std::shared_ptr<StreamTextMappedBuffer> create(uint64_t capacity);
  ~StreamTextMappedBuffer() override;

  size_t size() const override {
    return size_;
  }
  uint8_t *data() override {
    return base_;
  }

  StreamTextMappedBuffer(uint8_t *base, size_t size) : base_(base), size_(size) {}

 private:
  uint8_t *base_;
  size_t size_;
};

struct StreamTextFetchRequest {
  std::string url;
  std::string method;
  StreamTextHeaders headers;
  std::optional<std::string> body;
  /** Longest wait for the next bytes, in seconds; 0: the platform's default. */
  double timeoutSeconds{0};
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
