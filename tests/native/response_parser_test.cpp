#include "../../packages/react-native-text-stream/cpp/streaming/StreamTextResponseParser.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
using namespace facebook::react;
static size_t parserRuns = 0;
static size_t boundaryCuts = 0;

static StreamTextParseStatus run(StreamTextParserOptions o,
                                 const std::string &input, size_t split,
                                 std::string &out, size_t limit = SIZE_MAX) {
  ++parserRuns;
  StreamTextResponseParser p(std::move(o), [&](std::string_view s) {
    if (out.size() + s.size() > limit)
      return false;
    out.append(s);
    return true;
  });
  for (size_t i = 0; i < input.size(); i += split) {
    const size_t n = std::min(split, input.size() - i);
    if (p.feed(reinterpret_cast<const uint8_t *>(input.data() + i), n) !=
        StreamTextParseStatus::Continue)
      return p.status();
  }
  return p.finish();
}
static void
allSplits(StreamTextParserOptions o, const std::string &input,
          const std::string &expected,
          StreamTextParseStatus state = StreamTextParseStatus::Done) {
  for (size_t split = 1; split <= input.size() + 1; ++split) {
    std::string output;
    assert(run(o, input, split, output) == state);
    assert(output == expected);
  }
}
static void
allTwoChunkCuts(StreamTextParserOptions o, const std::string &input,
                const std::string &expected,
                StreamTextParseStatus wanted = StreamTextParseStatus::Done) {
  // Every legal network boundary, including just before/after CR and LF, must
  // give the same result.
  for (size_t cut = 0; cut <= input.size(); ++cut) {
    ++parserRuns;
    ++boundaryCuts;
    std::string output;
    StreamTextResponseParser p(o, [&](std::string_view s) {
      output.append(s);
      return true;
    });
    auto state = p.feed(reinterpret_cast<const uint8_t *>(input.data()), cut);
    if (state == StreamTextParseStatus::Continue)
      state = p.feed(reinterpret_cast<const uint8_t *>(input.data() + cut),
                     input.size() - cut);
    if (state == StreamTextParseStatus::Continue)
      state = p.finish();
    assert(state == wanted);
    assert(output == expected);
  }
}
static void terminalWithoutEof(StreamTextParserOptions o,
                               const std::string &input,
                               const std::string &expected,
                               StreamTextParseStatus wanted) {
  // A provider completion/error record must stop the socket during feed(),
  // before EOF arrives.
  for (size_t cut = 0; cut <= input.size(); ++cut) {
    ++parserRuns;
    ++boundaryCuts;
    std::string output;
    StreamTextResponseParser p(o, [&](std::string_view s) {
      output.append(s);
      return true;
    });
    auto state = p.feed(reinterpret_cast<const uint8_t *>(input.data()), cut);
    if (state == StreamTextParseStatus::Continue)
      state = p.feed(reinterpret_cast<const uint8_t *>(input.data() + cut),
                     input.size() - cut);
    assert(state == wanted);
    assert(output == expected);
  }
}
int main(int argc, char **argv) {
  const std::string generic =
      "\xEF\xBB\xBF: ping\r\nevent: delta\r\ndata: hello\r\ndata: world\r\nid: "
      "ignored\r\nretry: 10\r\n\r\n";
  allSplits({StreamTextProtocol::SSE, StreamTextExtraction::Data}, generic,
            "hello\nworld");
  // Regression: a CR only suppresses the immediately adjacent LF, even when the
  // next line and its LF are split across different network reads.
  allTwoChunkCuts({StreamTextProtocol::SSE, StreamTextExtraction::Data},
                  "data: first\rdata: second\n\n", "first\nsecond");
  allSplits({StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
            "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hi "
            "\\uD83D\\uDE00\"}}]}\n\ndata: "
            "{\"choices\":[{\"index\":0,\"delta\":{\"refusal\":\"no\"}}]}"
            "\n\ndata: [DONE]\n\n",
            "Hi \xF0\x9F\x98\x80no");
  allSplits(
      {StreamTextProtocol::SSE, StreamTextExtraction::OpenAIResponses},
      "event: response.output_text.delta\ndata: "
      "{\"type\":\"response.output_text.delta\",\"delta\":\"A\"}\n\nevent: "
      "response.completed\ndata: {\"type\":\"response.completed\"}\n\n",
      "A");
  allSplits({StreamTextProtocol::SSE, StreamTextExtraction::Anthropic},
            "event: content_block_start\ndata: "
            "{\"type\":\"content_block_start\",\"content_block\":{\"type\":"
            "\"text\",\"text\":\"A\"}}\n\nevent: content_block_delta\ndata: "
            "{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_"
            "delta\",\"text\":\"B\"}}\n\nevent: message_stop\ndata: "
            "{\"type\":\"message_stop\"}\n\n",
            "AB");
  allSplits({StreamTextProtocol::NDJSON, StreamTextExtraction::Ollama},
            "{\"message\":{\"content\":\"one\"}}\r\n{\"response\":\"two\","
            "\"done\":true}",
            "onetwo");
  allSplits({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
             "/a~1b/~0key"},
            "{\"a/b\":{\"~key\":\"ok\\u0000x\"}}\n", std::string("ok\0x", 4));
  allSplits({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer, ""},
            "\"root\"\n", "root");
  allTwoChunkCuts(
      {StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer, "/x"},
      "\xEF\xBB\xBF{\"x\":\"split\"}\r\n", "split");
  allSplits({StreamTextProtocol::SSE, StreamTextExtraction::Data},
            "data:\ndata: x\n\n", "\nx");
  allSplits({StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
            "data: "
            "{\"choices\":[{\"index\":1,\"delta\":{\"content\":\"skip\"}},{"
            "\"index\":0,\"delta\":{\"content\":\"take\"}}]}\n\ndata: "
            "[DONE]\n\ndata: {bad}\n\n",
            "take");
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::Data},
               "data: partial", 1, out) == StreamTextParseStatus::Done &&
           out.empty());
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x"},
               "{bad}\n", 1, out) == StreamTextParseStatus::Error);
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
               "data: {}\n\n", 1, out) == StreamTextParseStatus::Error);
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::Data, "", 4},
               "data: 12345\n\n", 1, out) == StreamTextParseStatus::Error);
  }
  // The framing limit applies to the joined data field, including the inserted
  // multi-data newline.
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::Data, "", 12},
               "data: 123456\ndata: 789012\n\n", 1,
               out) == StreamTextParseStatus::Error);
  }
  // A small valid record must not be rejected merely because yyjson's allocator
  // overhead exceeds an arbitrary multiple of the wire record length.
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x", 32},
               "{\"x\":\"ok\"}\n", 1, out) == StreamTextParseStatus::Done &&
           out == "ok");
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x"},
               std::string("{\"x\":\"") + char(0xff) + "\"}\n", 1,
               out) == StreamTextParseStatus::Error);
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x"},
               "{\"x\":\"\\uD800\"}\n", 1,
               out) == StreamTextParseStatus::Error);
  }
  // NDJSON permits only JSON whitespace (space/tab around a record); VT and FF
  // are not silently discarded as blank records because they are not JSON
  // whitespace.
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x"},
               "{\"x\":\"ok\"}\n\v\n", 1,
               out) == StreamTextParseStatus::Error &&
           out == "ok");
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x"},
               "{\"x\":\"ok\"}\n\f\n", 1,
               out) == StreamTextParseStatus::Error &&
           out == "ok");
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::Text, StreamTextExtraction::Data}, "abcdef",
               2, out, 3) == StreamTextParseStatus::Overflow);
  }
  // out()/done() must never turn an already-overflowed/error record into a
  // successful terminal.
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::Ollama},
               "{\"message\":{\"content\":\"xx\"},\"done\":true}\n", 1, out,
               1) == StreamTextParseStatus::Overflow &&
           out.empty());
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::Ollama},
               "{\"error\":\"no\",\"done\":true}\n", 1,
               out) == StreamTextParseStatus::Error);
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
               "data: "
               "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"A\","
               "\"refusal\":\"B\"}}]}\n\ndata: [DONE]\n\n",
               1, out, 1) == StreamTextParseStatus::Overflow &&
           out == "A");
  }

  allTwoChunkCuts({StreamTextProtocol::SSE, StreamTextExtraction::Data, "", 12},
                  "data: 12345\ndata: 67890\n\n", "12345\n67890");
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
               "data: "
               "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"XX\","
               "\"refusal\":\"B\"}}]}\n\ndata: [DONE]\n\n",
               1, out, 1) == StreamTextParseStatus::Overflow &&
           out.empty());
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
               "data: "
               "{\"error\":{},\"choices\":[{\"index\":0,\"delta\":{\"content\":"
               "\"ignored\"}}]}\n\n",
               1, out) == StreamTextParseStatus::Error &&
           out.empty());
  }
  {
    std::string out;
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::Ollama},
               "{\"message\":7,\"done\":true}\n", 1,
               out) == StreamTextParseStatus::Error &&
           out.empty());
  }
  // Grow and reuse the JSON pool across a large record, a small record and
  // another large one.
  {
    const std::string large(100000, 'x');
    std::string out;
    const auto input = "{\"x\":\"" + large +
                       "\"}\n{\"x\":\"small\"}\n{\"x\":\"" + large + "\"}\n";
    assert(run({StreamTextProtocol::NDJSON, StreamTextExtraction::JsonPointer,
                "/x"},
               input, 8192, out) == StreamTextParseStatus::Done);
    assert(out == large + "small" + large);
  }

  const std::string chatTerminal = "data: "
                                   "{\"choices\":[{\"index\":0,\"delta\":{"
                                   "\"content\":\"A\"}}]}\n\ndata: [DONE]\n\n";
  const std::string responsesTerminal =
      "event: response.output_text.delta\ndata: "
      "{\"type\":\"response.output_text.delta\",\"delta\":\"A\"}\n\nevent: "
      "response.completed\ndata: {\"type\":\"response.completed\"}\n\n";
  const std::string anthropicTerminal =
      "event: content_block_delta\ndata: "
      "{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
      "\"text\":\"A\"}}\n\nevent: message_stop\ndata: "
      "{\"type\":\"message_stop\"}\n\n";
  const std::string ollamaTerminal =
      "{\"message\":{\"content\":\"A\"},\"done\":true}\n";
  terminalWithoutEof(
      {StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat}, chatTerminal,
      "A", StreamTextParseStatus::Done);
  terminalWithoutEof(
      {StreamTextProtocol::SSE, StreamTextExtraction::OpenAIResponses},
      responsesTerminal, "A", StreamTextParseStatus::Done);
  terminalWithoutEof({StreamTextProtocol::SSE, StreamTextExtraction::Anthropic},
                     anthropicTerminal, "A", StreamTextParseStatus::Done);
  terminalWithoutEof({StreamTextProtocol::NDJSON, StreamTextExtraction::Ollama},
                     ollamaTerminal, "A", StreamTextParseStatus::Done);
  terminalWithoutEof(
      {StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
      "data: {\"error\":{\"message\":\"x\"}}\n\n", "",
      StreamTextParseStatus::Error);
  terminalWithoutEof(
      {StreamTextProtocol::SSE, StreamTextExtraction::OpenAIResponses},
      "event: error\ndata: {\"type\":\"error\"}\n\n", "",
      StreamTextParseStatus::Error);
  terminalWithoutEof({StreamTextProtocol::SSE, StreamTextExtraction::Anthropic},
                     "event: error\ndata: {\"type\":\"error\"}\n\n", "",
                     StreamTextParseStatus::Error);
  terminalWithoutEof({StreamTextProtocol::NDJSON, StreamTextExtraction::Ollama},
                     "{\"error\":\"x\"}\n", "", StreamTextParseStatus::Error);
  if (argc > 1 && std::string(argv[1]) == "--bench") {
    std::string input;
    for (int i = 0; i < 10000; ++i)
      input +=
          "data: "
          "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"token\"}}]}\n\n";
    input += "data: [DONE]\n\n";
    for (size_t chunk : {size_t(1), size_t(64), size_t(8192)}) {
      std::string out;
      auto started = std::chrono::steady_clock::now(), first = started;
      StreamTextResponseParser p(
          {StreamTextProtocol::SSE, StreamTextExtraction::OpenAIChat},
          [&](std::string_view s) {
            if (out.empty())
              first = std::chrono::steady_clock::now();
            out.append(s);
            return true;
          });
      for (size_t i = 0;
           i < input.size() && p.status() == StreamTextParseStatus::Continue;
           i += chunk) {
        const auto state =
            p.feed(reinterpret_cast<const uint8_t *>(input.data() + i),
                   std::min(chunk, input.size() - i));
        assert(state == StreamTextParseStatus::Continue ||
               state == StreamTextParseStatus::Done);
      }
      assert(p.finish() == StreamTextParseStatus::Done && out.size() == 50000);
      const double seconds = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      const double ttfe =
          std::chrono::duration<double, std::milli>(first - started).count();
      std::cout << "chunk " << chunk << " ttfe_ms " << ttfe << " MBps "
                << input.size() / seconds / 1e6 << " output " << out.size()
                << "\n";
    }
  }
  std::cout << "response parser native checks " << parserRuns
            << " (all-boundary two-chunk cuts " << boundaryCuts << ")\n";
}
