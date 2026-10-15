#include "../../packages/react-native-text-stream/cpp/markdown/StreamMarkdown.h"

#include <cassert>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct RunCopy {
  size_t start, length;
  uint32_t flags;
  int heading, indent;
  std::string url;
  bool operator==(const RunCopy& other) const {
    return start == other.start && length == other.length && flags == other.flags &&
        heading == other.heading && indent == other.indent && url == other.url;
  }
};
struct DocumentCopy {
  std::u16string text;
  std::vector<RunCopy> runs;
};
static DocumentCopy copyDocument(const STMarkdownDocument* document) {
  assert(document && document->error == 0);
  DocumentCopy copy;
  if (document->textLength != 0) {
    assert(document->text);
    copy.text.assign(reinterpret_cast<const char16_t*>(document->text), document->textLength);
  }
  for (size_t i = 0; i < document->runCount; ++i) {
    const auto& run = document->runs[i];
    copy.runs.push_back({run.start, run.length, run.flags, run.headingLevel, run.indent, run.url ? run.url : ""});
  }
  return copy;
}
static void assertSame(const DocumentCopy& a, const DocumentCopy& b) {
  assert(a.text == b.text);
  assert(a.runs == b.runs);
}
struct StyledCopy {
  std::u16string text;
  struct Style {
    uint32_t flags = 0;
    int heading = 0;
    int indent = 0;
    std::string url;
    bool operator==(const Style& other) const {
      return flags == other.flags && heading == other.heading && indent == other.indent && url == other.url;
    }
  };
  std::vector<Style> style;
};
static StyledCopy styledCopy(const DocumentCopy& copy) {
  StyledCopy result;
  result.text = copy.text;
  result.style.resize(copy.text.size());
  for (const auto& run : copy.runs) {
    for (size_t i = run.start; i < run.start + run.length; ++i) {
      assert(i < result.style.size());
      result.style[i] = {run.flags, run.heading, run.indent, run.url};
    }
  }
  return result;
}
static void applyPatch(StyledCopy& current, const DocumentCopy& next, size_t start) {
  assert(start <= current.text.size() && start <= next.text.size());
  const auto styled = styledCopy(next);
  current.text.resize(start); current.style.resize(start);
  current.text.append(styled.text.begin() + start, styled.text.end());
  current.style.insert(current.style.end(), styled.style.begin() + start, styled.style.end());
}
static void assertStreamSeam(const std::string& source, bool github) {
  auto* retained = stmd_create();
  assert(retained);
  StyledCopy visible;
  for (size_t length = 0; length <= source.size(); ++length) {
    const auto* retainedDoc = stmd_parse(retained, source.data(), length, github);
    const auto actual = copyDocument(retainedDoc);
    auto* fresh = stmd_create();
    assert(fresh);
    const auto expected = copyDocument(stmd_parse(fresh, source.data(), length, github));
    stmd_destroy(fresh);
    assertSame(actual, expected);
    assert(retainedDoc->changedStartUTF16 <= actual.text.size());
    applyPatch(visible, actual, retainedDoc->changedStartUTF16);
    const auto expectedStyled = styledCopy(actual);
    assert(visible.text == expectedStyled.text && visible.style == expectedStyled.style);
  }
  stmd_destroy(retained);
}

static uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5;
  return state;
}
static std::string ascii(const STMarkdownDocument* d);
static bool has(const STMarkdownDocument* d, uint32_t flag);
static void fuzzDeterministic() {
  uint32_t state = 0x51a7e5u;
  for (int sample = 0; sample < 128; ++sample) {
    std::string input;
    const size_t length = nextRandom(state) % 257;
    input.reserve(length);
    for (size_t i = 0; i < length; ++i) input.push_back(static_cast<char>(nextRandom(state) & 0xff));
    assertStreamSeam(input, (nextRandom(state) & 1) != 0);
  }
}

static void assertAbiErrorAndRecovery() {
  assert(stmd_parse(nullptr, "x", 1, true) == nullptr);
  auto* parser = stmd_create();
  assert(parser);
  const auto* invalid = stmd_parse(parser, nullptr, 1, true);
  assert(invalid && invalid->error != 0 && invalid->sourceBytesConsumed == 0);
  const auto* empty = stmd_parse(parser, nullptr, 0, true);
  assert(empty && empty->error == 0 && empty->textLength == 0 && empty->runCount == 0);
#if SIZE_MAX > UINT_MAX
  const auto* oversized = stmd_parse(parser, "x", static_cast<size_t>(UINT_MAX) + 1u, true);
  assert(oversized && oversized->error != 0 && oversized->sourceBytesConsumed == 0);
#endif
  const std::string recovered = "**recovered**";
  const auto valid = copyDocument(stmd_parse(parser, recovered.data(), recovered.size(), true));
  auto* fresh = stmd_create();
  assertSame(valid, copyDocument(stmd_parse(fresh, recovered.data(), recovered.size(), true)));
  stmd_destroy(fresh);
  stmd_destroy(parser);
}

static void assertBorrowedDocumentSnapshot() {
  auto* parser = stmd_create();
  assert(parser);
  const std::string first = "[first](https://example.test/first)";
  const auto* borrowed = stmd_parse(parser, first.data(), first.size(), true);
  assert(borrowed && borrowed->text && borrowed->runs && borrowed->runCount != 0);
  // The API is intentionally borrowed. Consumers that need a previous document
  // after this parse must copy it before making the next parser call.
  const DocumentCopy snapshot = copyDocument(borrowed);
  const std::string second = "**second**";
  const auto* next = stmd_parse(parser, second.data(), second.size(), true);
  assert(next && next->error == 0);
  auto* fresh = stmd_create();
  assertSame(snapshot, copyDocument(stmd_parse(fresh, first.data(), first.size(), true)));
  stmd_destroy(fresh);
  stmd_destroy(parser);
}

static void assertDialectDifference() {
  const std::string source = "~~gone~~\n\n- [x] done\n\nA | B\n--|--\n1 | 2\n";
  auto* parser = stmd_create();
  const auto* common = stmd_parse(parser, source.data(), source.size(), false);
  const auto commonText = ascii(common);
  assert(common->error == 0 && !has(common, STMD_RUN_STRIKE));
  assert(commonText.find("~~gone~~") != std::string::npos);
  const auto* github = stmd_parse(parser, source.data(), source.size(), true);
  const auto githubText = ascii(github);
  assert(github->error == 0 && has(github, STMD_RUN_STRIKE));
  assert(githubText.find("gone") != std::string::npos && githubText.find("~~gone~~") == std::string::npos);
  stmd_destroy(parser);
}

static std::string ascii(const STMarkdownDocument* d) {
  std::string result;
  for (size_t i = 0; i < d->textLength; ++i) {
    auto c = d->text[i];
    if (c < 128) result.push_back(static_cast<char>(c)); else result += '?';
  }
  return result;
}
static bool has(const STMarkdownDocument* d, uint32_t flag) {
  for (size_t i = 0; i < d->runCount; ++i) if ((d->runs[i].flags & flag) != 0) return true;
  return false;
}
int main(int argc, char** argv) {
  STMarkdownParser* parser = stmd_create();
  assert(parser);
  const std::string sample =
      "# H &amp; \xf0\x9f\x98\x80\n\n> **bold _italic_** and ~~old~~ [link](https://example.com/a?q=1)\n\n"
      "- [x] done\n  - child\n\n```js\nconst x = `code`;\n```\n\n"
      "a | b\n--|--\n1 | 2\n\n[ref]: https://example.org\n[referenced][ref]\n";
  const auto* doc = stmd_parse(parser, sample.data(), sample.size(), true);
  assert(doc && doc->error == 0 && doc->sourceBytesConsumed == sample.size());
  const std::string rendered = ascii(doc);
  assert(rendered.find("H &") != std::string::npos);
  assert(rendered.find("done") != std::string::npos && rendered.find("const x") != std::string::npos);
  assert(has(doc, STMD_RUN_BOLD) && has(doc, STMD_RUN_ITALIC) && has(doc, STMD_RUN_STRIKE));
  assert(has(doc, STMD_RUN_LINK) && has(doc, STMD_RUN_CODE_BLOCK) && has(doc, STMD_RUN_QUOTE));
  bool linkOk = false;
  for (size_t i = 0; i < doc->runCount; ++i) {
    if ((doc->runs[i].flags & STMD_RUN_LINK) && doc->runs[i].url && std::string(doc->runs[i].url) == "https://example.com/a?q=1") linkOk = true;
  }
  assert(linkOk);
  for (size_t i = 1; i < doc->runCount; ++i) {
    const auto& left = doc->runs[i - 1]; const auto& right = doc->runs[i];
    assert(left.start + left.length != right.start || left.flags != right.flags || left.headingLevel != right.headingLevel || left.indent != right.indent);
  }
  // Every prefix must be safe: a stream can end inside any UTF-8 sequence or
  // Markdown construct. This checks parser safety, not semantic equality.
  for (size_t i = 0; i <= sample.size(); ++i) {
    doc = stmd_parse(parser, sample.data(), i, true);
    assert(doc && doc->error == 0 && doc->sourceBytesConsumed == i);
  }
  const std::string adversarial =
      "3. third\n4. fourth\n   1. nested\n      1. deep\n\n"
      "soft\nbreak  \n\nhard\n\n"
      "[escaped](https://example.com/?a=1&amp;b=2)\n"
      "**outer _inner_ outer**\n";
  doc = stmd_parse(parser, adversarial.data(), adversarial.size(), true);
  assert(doc->error == 0);
  const std::string readable = ascii(doc);
  assert(readable.find("3. third") != std::string::npos && readable.find("1. deep") != std::string::npos);
  assert(readable.find("soft break") != std::string::npos && readable.find("hard") != std::string::npos);
  bool decodedHref = false, nestedStyle = false;
  for (size_t i = 0; i < doc->runCount; ++i) {
    const auto& run = doc->runs[i];
    if ((run.flags & STMD_RUN_LINK) && run.url && std::string(run.url) == "https://example.com/?a=1&b=2") decodedHref = true;
    if ((run.flags & (STMD_RUN_BOLD | STMD_RUN_ITALIC)) == (STMD_RUN_BOLD | STMD_RUN_ITALIC)) nestedStyle = true;
  }
  assert(decodedHref && nestedStyle);
  std::string hostile(512, '>');
  hostile += " invalid: \xf0\x28\x8c\x28 &#x110000; &#999999999999999999999;\n";
  doc = stmd_parse(parser, hostile.data(), hostile.size(), true);
  assert(doc->error == 0 && doc->textLength > 0);
  const std::string bom = "\xef\xbb\xbf\xef\xbb\xbfvisible";
  doc = stmd_parse(parser, bom.data(), bom.size(), true);
  assert(doc->error == 0 && doc->textLength == 9 && doc->text[0] == 0xfeff && doc->text[1] == 'v');
  doc = stmd_parse_decoded(parser, bom.data(), bom.size(), true);
  assert(doc->error == 0 && doc->textLength == 10 && doc->text[0] == 0xfeff && doc->text[1] == 0xfeff);
  const std::string grin = "\xf0\x9f\x98\x80";
  const std::string grin2 = "\xf0\x9f\x98\x81";
  assert(stmd_parse(parser, grin.data(), grin.size(), true)->error == 0);
  doc = stmd_parse(parser, grin2.data(), grin2.size(), true);
  // A UTF-16 diff is intentionally exact; platform consumers must expand this
  // low-surrogate offset to a composed-character boundary before replacing UI text.
  assert(doc->changedStartUTF16 == 1);

  // These inputs exercise every byte boundary, including a late reference
  // definition that restyles already-visible text. The reconstructed UI state
  // must exactly match a fresh parse after every append.
  const std::vector<std::pair<std::string, bool>> streamingCases = {
      {"[late reference][id]\n\n[id]: https://example.test/?a=1&amp;b=2\n", true},
      {"> - **outer _inner_** [link](https://example.test)\n>   - `code` ~~old~~\n", true},
      {"A | B\n--|--\n1 | [two][x]\n\n[x]: https://example.test/x\n", true},
      {"\xef\xbb\xbf# heading \xf0\x9f\x98\x80\n\n```\n* literal *\n```\n", true},
      {"~~literal in CommonMark~~\n\n[ref][r]\n\n[r]: /where\n", false},
  };
  for (const auto& streaming : streamingCases) assertStreamSeam(streaming.first, streaming.second);

  std::string deep;
  for (int depth = 0; depth < 96; ++depth) deep.append(static_cast<size_t>(depth) * 2, ' ').append("- nested\n");
  doc = stmd_parse(parser, deep.data(), deep.size(), true);
  assert(doc->error == 0 && doc->textLength > 0);
  assertAbiErrorAndRecovery();
  assertBorrowedDocumentSnapshot();
  assertDialectDifference();
  fuzzDeterministic();
  if (argc > 1 && std::string(argv[1]) == "--bench") {
    for (size_t bytes : {size_t(5 * 1024), size_t(100 * 1024)}) {
      std::string input;
      while (input.size() < bytes) input += "- **Fast** [link](https://example.com) &amp; `code`\n";
      input.resize(bytes);
      const auto start = std::chrono::steady_clock::now();
      constexpr int iterations = 100;
      for (int i = 0; i < iterations; ++i) assert(stmd_parse(parser, input.data(), input.size(), true)->error == 0);
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::cout << "retained markdown " << bytes << " bytes: " << (bytes * iterations / elapsed / 1e6) << " MB/s\n";
      const auto createStart = std::chrono::steady_clock::now();
      for (int i = 0; i < iterations; ++i) {
        auto* fresh = stmd_create();
        assert(fresh && stmd_parse(fresh, input.data(), input.size(), true)->error == 0);
        stmd_destroy(fresh);
      }
      const auto createSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - createStart).count();
      std::cout << "create markdown " << bytes << " bytes: " << (bytes * iterations / createSeconds / 1e6) << " MB/s\n";
      for (size_t delta : {size_t(17), size_t(128), size_t(1024)}) {
        std::string streamed = input.substr(0, input.size() - delta);
        stmd_parse(parser, streamed.data(), streamed.size(), true);
        streamed += input.substr(input.size() - delta);
        const auto deltaStart = std::chrono::steady_clock::now();
        doc = stmd_parse(parser, streamed.data(), streamed.size(), true);
        const auto deltaSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - deltaStart).count();
        assert(doc->error == 0 && doc->changedStartUTF16 <= doc->textLength);
        std::cout << "delta " << delta << " after " << bytes << " bytes: " << (deltaSeconds * 1e3) << " ms, changed " << doc->changedStartUTF16 << " UTF-16\n";
      }
      for (size_t delta : {size_t(17), size_t(128), size_t(1024)}) {
        std::vector<double> samples;
        size_t reparsedBytes = 0;
        for (size_t length = delta; length < input.size(); length += delta) {
          const auto tick = std::chrono::steady_clock::now();
          doc = stmd_parse(parser, input.data(), length, true);
          samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick).count());
          assert(doc->error == 0);
          reparsedBytes += length;
        }
        const auto tick = std::chrono::steady_clock::now();
        doc = stmd_parse(parser, input.data(), input.size(), true);
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick).count());
        reparsedBytes += input.size();
        std::sort(samples.begin(), samples.end());
        const auto percentile = [&samples](double q) { return samples[static_cast<size_t>(q * static_cast<double>(samples.size() - 1))]; };
        double totalMs = 0; for (double v : samples) totalMs += v;
        std::cout << "stream " << delta << " -> " << bytes << " bytes: " << samples.size() << " parses, " << totalMs
                  << " ms total, p50/p95/p99 " << percentile(.50) << '/' << percentile(.95) << '/' << percentile(.99)
                  << " ms, source amplification " << (static_cast<double>(reparsedBytes) / input.size()) << "x\n";
      }
    }
  }
  stmd_destroy(parser);
}
