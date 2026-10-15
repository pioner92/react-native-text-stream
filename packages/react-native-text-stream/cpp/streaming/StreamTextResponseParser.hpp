#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace facebook::react {

enum class StreamTextProtocol { Text, SSE, NDJSON };
enum class StreamTextExtraction {
  Data,
  JsonPointer,
  OpenAIChat,
  OpenAIResponses,
  Anthropic,
  Ollama
};

struct StreamTextParserOptions {
  StreamTextProtocol protocol{StreamTextProtocol::Text};
  StreamTextExtraction extraction{StreamTextExtraction::Data};
  std::string textPointer;
  size_t maxEventBytes{1024 * 1024};
};

// Numeric terminal values match TSB1. A sink may append the prefix that fits
// before returning false. Callers serialize feed/finish with cancellation; no
// callback enters JavaScript.
enum class StreamTextParseStatus : uint32_t {
  Continue = 0,
  Done = 1,
  Error = 2,
  Overflow = 4
};

class StreamTextResponseParser {
public:
  using Sink = std::function<bool(std::string_view)>;
  StreamTextResponseParser(StreamTextParserOptions options, Sink sink);
  ~StreamTextResponseParser();
  StreamTextResponseParser(const StreamTextResponseParser &) = delete;
  StreamTextResponseParser &
  operator=(const StreamTextResponseParser &) = delete;
  StreamTextParseStatus feed(const uint8_t *bytes, size_t size);
  StreamTextParseStatus finish();
  StreamTextParseStatus status() const;
  const std::string &error() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace facebook::react
