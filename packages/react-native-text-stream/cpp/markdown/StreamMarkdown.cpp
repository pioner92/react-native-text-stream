#include "StreamMarkdown.h"
#include "StreamMarkdownSyntax.h"

extern "C" {
#include "vendor/md4c/entity.h"
}
#include "vendor/md4c/md4c.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

struct STMarkdownParser {
  struct SavedRun { size_t start, length; uint32_t flags; int heading, indent; std::string url; };
  struct SavedCell { std::vector<uint16_t> text; std::vector<SavedRun> runs; uint32_t flags; int alignment; };
  struct SavedBlock { size_t start, length; uint32_t kind; size_t rows, columns; std::string url; std::vector<uint16_t> alt; std::vector<SavedCell> cells; };
  struct CellOwned { std::vector<uint16_t> text; std::vector<STMarkdownRun> runs; uint32_t flags; int alignment; };
  std::vector<uint16_t> text;
  std::vector<STMarkdownRun> runs;
  std::vector<std::string> urls;
  std::deque<std::string> runUrls;
  std::vector<STMarkdownBlock> blocks;
  std::deque<std::string> blockUrls;
  std::deque<std::vector<uint16_t>> blockAlts;
  std::deque<std::vector<CellOwned>> tableOwned;
  std::deque<std::vector<STMarkdownCell>> tablePublic;
  std::vector<size_t> tableBlockIndices;
  int tableDepth = 0;
  size_t activeTable = static_cast<size_t>(-1), activeCell = static_cast<size_t>(-1);
  size_t tableColumns = 0, tableRows = 0;
  int imageDepth = 0;
  size_t activeImage = static_cast<size_t>(-1);
  std::vector<uint16_t> imageAlt;
  STMarkdownDocument document{};
  uint32_t flags = 0;
  int heading = 0;
  int listDepth = 0;
  struct List { bool ordered; unsigned next; };
  std::vector<List> lists;
  int quoteDepth = 0;
  int codeDepth = 0;
  size_t codeStart = 0;
  size_t codeRunStart = 0;
  stmd::Language codeLanguage = stmd::Language::none;
  int boldDepth = 0, italicDepth = 0, strikeDepth = 0, inlineCodeDepth = 0;
  bool rowHasCell = false;
  std::vector<uint16_t> previousText;
  std::vector<SavedRun> previousRuns;
  std::vector<SavedBlock> previousBlocks;
};

namespace {

constexpr size_t kStreamingTailBytes = 8192;

static bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

static bool previousLineStart(const char* input, size_t start, size_t lowerBound, size_t* result) {
  if (start == 0) { *result = 0; return true; }
  size_t at = start - 1;
  while (at > lowerBound && input[at - 1] != '\n') --at;
  // Do not let a table candidate walk back through an arbitrarily long
  // previous line. Its origin is outside the bounded projection window.
  if (at == lowerBound && lowerBound != 0 && input[at - 1] != '\n') return false;
  *result = at;
  return true;
}

static size_t lineEnd(const char* input, size_t end) {
  return end > 0 && input[end - 1] == '\n' ? end - 1 : end;
}

static bool isPotentialTableDelimiter(const char* input, size_t start, size_t end) {
  bool pipe = false;
  for (size_t i = start; i < end; ++i) {
    const char c = input[i];
    if (c == '|') pipe = true;
    else if (c == '-') continue;
    else if (c != ':' && !isSpace(c)) return false;
  }
  return pipe;
}

static bool isCompleteTableDelimiter(const char* input, size_t start, size_t end) {
  while (start < end && isSpace(input[start])) ++start;
  while (end > start && isSpace(input[end - 1])) --end;
  if (start < end && input[start] == '|') ++start;
  if (end > start && input[end - 1] == '|') --end;
  bool any = false;
  for (size_t part = start; part <= end;) {
    size_t nextSeparator = part;
    while (nextSeparator < end && input[nextSeparator] != '|') ++nextSeparator;
    size_t contentEnd = nextSeparator;
    while (part < contentEnd && isSpace(input[part])) ++part;
    while (contentEnd > part && isSpace(input[contentEnd - 1])) --contentEnd;
    if (part == contentEnd) return false;
    if (input[part] == ':') ++part;
    if (contentEnd > part && input[contentEnd - 1] == ':') --contentEnd;
    size_t dashes = 0;
    while (part < contentEnd && input[part] == '-') { ++dashes; ++part; }
    // Wait for the canonical three-dash delimiter while streaming. MD4C also
    // accepts shorter forms, but delaying those avoids a grid/plain-text flip.
    if (part != contentEnd || dashes < 3) return false;
    any = true;
    if (nextSeparator == end) break;
    part = nextSeparator + 1;
  }
  return any;
}

enum class TailKind { none, strong, strike, emphasis, code, linkLabel, linkTarget };
struct TailProjection {
  size_t start;
  TailKind kind;
  char markers[4];
  unsigned char widths[4];
  unsigned char closerCount;
};

// This is deliberately a single forward pass over at most one final line.
// It never walks backward over escape runs, which keeps adversarial backslash
// tails linear. Fenced blocks are protected by a parse probe below.
static TailProjection unfinishedInline(const char* input, size_t start, size_t end) {
  constexpr size_t none = static_cast<size_t>(-1);
  size_t bracket = none, pairedLink = none, strong = none, strike = none, emphasis = none, code = none;
  char emphasisMarker = 0;
  size_t codeCount = 0;
  size_t linkParenDepth = 0;
  bool strongPendingClose = false;
  bool strikePendingClose = false;
  bool escaped = false;
  bool finalByteEscaped = false;
  bool finalBytePartOfPair = false;
  for (size_t i = start; i < end; ++i) {
    const char c = input[i];
    if (escaped) {
      if (i + 1 == end) finalByteEscaped = true;
      escaped = false;
      continue;
    }
    if (c == '\\') { escaped = true; continue; }
    if (c == '`') {
      size_t count = 1; while (i + count < end && input[i + count] == '`') ++count;
      if (code == none) { code = i; codeCount = count; }
      else if (count == codeCount) { code = none; codeCount = 0; }
      i += count - 1;
      continue;
    }
    if (code != none) continue;
    if (c == '[') { bracket = i > start && input[i - 1] == '!' ? i - 1 : i; pairedLink = none; continue; }
    if (c == ']' && bracket != none) {
      if (i + 1 < end && input[i + 1] == '(') { pairedLink = bracket; linkParenDepth = 1; ++i; }
      else if (i + 1 < end) bracket = none;
      continue;
    }
    if (pairedLink != none && c == '(') { ++linkParenDepth; continue; }
    if (c == ')' && pairedLink != none) {
      if (--linkParenDepth == 0) bracket = pairedLink = none;
      continue;
    }
    if (i + 1 < end && c == '~' && input[i + 1] == '~') {
      if (i + 2 == end) finalBytePartOfPair = true;
      strike = strike == none ? i : none;
      ++i;
      continue;
    }
    if (c == '~' && strike != none && i + 1 == end) { strikePendingClose = true; continue; }
    if (i + 1 < end && (c == '*' || c == '_') && input[i + 1] == c) {
      if (i + 2 == end) finalBytePartOfPair = true;
      strong = strong == none ? i : none;
      ++i;
      continue;
    }
    if (c == '*' || c == '_') {
      // The first byte of a strong closer is common in a byte stream. Treat
      // it as a pending strong close, not a nested emphasis opener.
      if (strong != none && c == input[strong] && i + 1 == end) { strongPendingClose = true; continue; }
      if (emphasis != none && c == emphasisMarker) {
        if (i + 1 == end) finalBytePartOfPair = true;
        emphasis = none;
        emphasisMarker = 0;
      }
      else if (c != '_' || i == start || !std::isalnum(static_cast<unsigned char>(input[i - 1]))) {
        emphasis = i; emphasisMarker = c;
      }
    }
  }
  if (pairedLink != none) return {pairedLink, TailKind::linkTarget, {}, {}, 0};
  if (bracket != none) return {bracket, TailKind::linkLabel, {}, {}, 0};
  if (code != none) return {code, TailKind::code, {'`'}, {static_cast<unsigned char>(codeCount)}, 1};
  struct Delimiter { size_t start; char marker; unsigned char width; } active[3];
  size_t activeCount = 0;
  if (strong != none) active[activeCount++] = {strong, input[strong], static_cast<unsigned char>(strongPendingClose ? 1 : 2)};
  if (strike != none) active[activeCount++] = {strike, '~', static_cast<unsigned char>(strikePendingClose ? 1 : 2)};
  if (emphasis != none) active[activeCount++] = {emphasis, emphasisMarker, 1};
  if (activeCount == 0) {
    // Withhold a lone unescaped marker at the transport boundary. It may be
    // the first byte of a pair, and Markdown has no readable payload yet.
    if (end > start && !finalByteEscaped && !finalBytePartOfPair &&
        (input[end - 1] == '*' || input[end - 1] == '~')) {
      return {end - 1, TailKind::emphasis, {}, {}, 0};
    }
    return {end, TailKind::none, {}, {}, 0};
  }
  // A closing delimiter closes the innermost still-open span first.
  for (size_t i = 0; i < activeCount; ++i) {
    for (size_t j = i + 1; j < activeCount; ++j) {
      if (active[i].start < active[j].start) std::swap(active[i], active[j]);
    }
  }
  TailProjection result{active[activeCount - 1].start, TailKind::strong, {}, {}, static_cast<unsigned char>(activeCount)};
  for (size_t i = 0; i < activeCount; ++i) {
    result.markers[i] = active[i].marker;
    result.widths[i] = active[i].width;
    result.start = std::min(result.start, active[i].start);
  }
  return result;
}

static bool makeStreamingProjection(const char* input, size_t length, std::string* output) {
  if (length == 0) return false;
  const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
  size_t currentStart = length;
  while (currentStart > boundedStart && input[currentStart - 1] != '\n') --currentStart;
  // A megabyte-long line is intentionally left to the final parser rather
  // than making the progressive tail scan or projection unbounded.
  if (currentStart == boundedStart && boundedStart != 0 && input[currentStart - 1] != '\n') return false;
  // MD4C does not materialize a table until it has a row after the delimiter.
  // Keep header and delimiter hidden through that transition, then leave every
  // later row to the normal parser.
  bool confirmedTable = false;
  bool sawRowAfterDelimiter = false;
  for (size_t end = currentStart; end > boundedStart;) {
    size_t start = 0;
    if (!previousLineStart(input, end, boundedStart, &start)) break;
    const size_t rowEnd = lineEnd(input, end);
    if (std::memchr(input + start, '|', rowEnd - start) == nullptr) break;
    if (isCompleteTableDelimiter(input, start, rowEnd)) {
      confirmedTable = sawRowAfterDelimiter;
      break;
    }
    sawRowAfterDelimiter = true;
    if (start == 0 || start == end) break;
    end = start;
  }
  if (!confirmedTable) {
    // The first data-row byte follows an already complete delimiter. MD4C
    // still exposes the header as plain text at this point, so retain the
    // whole candidate until that row is complete.
    if (currentStart < length && currentStart > 0) {
      size_t delimiter = 0;
      if (previousLineStart(input, currentStart, boundedStart, &delimiter) &&
          isCompleteTableDelimiter(input, delimiter, lineEnd(input, currentStart))) {
        size_t header = 0;
        if (delimiter > 0 && previousLineStart(input, delimiter, boundedStart, &header) &&
            std::memchr(input + header, '|', delimiter - header) != nullptr) {
          output->assign(input, header);
          return true;
        }
      }
    }
    // A complete delimiter followed by its newline still has no data row.
    // Resolve this direct case before walking older lines.
    if (currentStart == length && currentStart > 0) {
      size_t delimiter = 0;
      if (previousLineStart(input, currentStart, boundedStart, &delimiter) &&
          isPotentialTableDelimiter(input, delimiter, lineEnd(input, currentStart))) {
        size_t header = 0;
        if (delimiter > 0 && previousLineStart(input, delimiter, boundedStart, &header) &&
            std::memchr(input + header, '|', delimiter - header) != nullptr) {
          output->assign(input, header);
          return true;
        }
      }
    }
    // Locate the pending delimiter, whether it is the current fragment or a
    // completed line followed by the first row. Its preceding pipe line is
    // the header to suppress.
    size_t delimiterStart = currentStart;
    size_t delimiterLimit = length;
    for (;;) {
      const size_t candidateEnd = lineEnd(input, delimiterLimit);
      if (candidateEnd < delimiterStart) break;
      if (std::memchr(input + delimiterStart, '|', candidateEnd - delimiterStart) == nullptr) break;
      if (isPotentialTableDelimiter(input, delimiterStart, candidateEnd)) {
        size_t header = 0;
        if (delimiterStart > 0 && previousLineStart(input, delimiterStart, boundedStart, &header) &&
            std::memchr(input + header, '|', delimiterStart - header) != nullptr) {
          output->assign(input, header);
          return true;
        }
      }
      if (delimiterStart == 0 || delimiterStart <= boundedStart) break;
      delimiterLimit = delimiterStart;
      if (!previousLineStart(input, delimiterStart, boundedStart, &delimiterStart)) break;
    }
    if (currentStart == length && currentStart > 0) {
      size_t header = 0;
      if (previousLineStart(input, currentStart, boundedStart, &header) &&
          std::memchr(input + header, '|', currentStart - header) != nullptr) {
        output->assign(input, header);
        return true;
      }
    }
  }
  // Hold the plausible header from the first pipe too. This prevents a raw
  // header from flashing while the transport is still delivering its delimiter.
  size_t lineContent = currentStart;
  while (lineContent < length && input[lineContent] == ' ') ++lineContent;
  if (!confirmedTable && lineContent < length && input[lineContent] == '|' &&
      std::memchr(input + lineContent, '|', length - lineContent) != nullptr) {
    output->assign(input, currentStart);
    return true;
  }
  if (length > currentStart && input[length - 1] == '!' &&
      (length == currentStart + 1 || input[length - 2] != '\\')) {
    output->assign(input, length - 1);
    return true;
  }
  const TailProjection tail = unfinishedInline(input, currentStart, length);
  if (tail.kind == TailKind::none) return false;
  output->assign(input, tail.start);
  const size_t openerWidth = (input[tail.start] == '*' || input[tail.start] == '_' || input[tail.start] == '~') &&
      tail.start + 1 < length && input[tail.start + 1] == input[tail.start] ? 2 : 1;
  // A delimiter by itself has no readable payload. Withhold it rather than
  // manufacturing a visible doubled marker.
  if (tail.start + openerWidth == length) return true;
  if (tail.kind == TailKind::strong || tail.kind == TailKind::strike || tail.kind == TailKind::emphasis) {
    // A Markdown closing delimiter cannot follow whitespace. Preserve a
    // streamed trailing space outside the provisional styled span instead.
    size_t payloadEnd = length;
    while (payloadEnd > tail.start && isSpace(input[payloadEnd - 1])) --payloadEnd;
    output->append(input + tail.start, payloadEnd - tail.start);
    for (size_t i = 0; i < tail.closerCount; ++i) output->append(tail.widths[i], tail.markers[i]);
    output->append(input + payloadEnd, length - payloadEnd);
  } else if (tail.kind == TailKind::code) {
    output->append(input + tail.start, length - tail.start);
    output->append(tail.widths[0], '`');
  }
  else {
    const size_t marker = input[tail.start] == '!' ? 2 : 1;
    const size_t labelStart = tail.start + marker;
    size_t closing = tail.kind == TailKind::linkTarget
        ? static_cast<size_t>(std::find(input + labelStart, input + length, ']') - input)
        : length;
    if (tail.kind == TailKind::linkLabel && closing > labelStart && input[closing - 1] == ']') --closing;
    // A half-link/image is rendered as its readable label, never its URL.
    output->append(input + labelStart, closing - labelStart);
  }
  return true;
}

// Used only after the bounded tail scan found a construct worth projecting.
// Matching marker kind and width avoids treating an in-code shorter fence as a
// closing delimiter.
static bool hasOpenFencedBlock(const char* input, size_t length) {
  char marker = 0;
  size_t markerCount = 0;
  for (size_t start = 0; start < length;) {
    size_t end = start;
    while (end < length && input[end] != '\n') ++end;
    size_t at = start;
    while (at < end && input[at] == ' ') ++at;
    if (at < end && input[at] == '>') { ++at; if (at < end && input[at] == ' ') ++at; }
    size_t count = 0;
    const char c = at < end ? input[at] : 0;
    while (at + count < end && input[at + count] == c) ++count;
    if ((c == '`' || c == '~') && count >= 3) {
      if (marker == 0) { marker = c; markerCount = count; }
      else if (marker == c && count >= markerCount) { marker = 0; markerCount = 0; }
    }
    start = end == length ? length : end + 1;
  }
  return marker != 0;
}

static void appendCodepoint(STMarkdownParser* p, unsigned cp) {
  if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
  if (cp <= 0xffff) { p->text.push_back(static_cast<uint16_t>(cp)); return; }
  cp -= 0x10000;
  p->text.push_back(static_cast<uint16_t>(0xd800 + (cp >> 10)));
  p->text.push_back(static_cast<uint16_t>(0xdc00 + (cp & 0x3ff)));
}
static void appendUtf8Codepoint(std::string& out, unsigned cp) {
  if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
  if (cp < 0x80) out.push_back(static_cast<char>(cp));
  else if (cp < 0x800) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
  else if (cp < 0x10000) { out.push_back(static_cast<char>(0xe0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
  else { out.push_back(static_cast<char>(0xf0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
}

static uint32_t codeLanguageFlag(stmd::Language language) {
  switch (language) {
    case stmd::Language::javascript: return STMD_RUN_LANGUAGE_JAVASCRIPT;
    case stmd::Language::typescript: return STMD_RUN_LANGUAGE_TYPESCRIPT;
    case stmd::Language::json: return STMD_RUN_LANGUAGE_JSON;
    case stmd::Language::python: return STMD_RUN_LANGUAGE_PYTHON;
    default: return 0;
  }
}

static void appendUtf8(STMarkdownParser* p, const char* s, size_t n) {
  for (size_t i = 0; i < n;) {
    unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) { appendCodepoint(p, c); continue; }
    unsigned cp = 0; size_t extra = 0;
    if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
    else { appendCodepoint(p, 0xfffd); continue; }
    if (i + extra > n) { appendCodepoint(p, 0xfffd); break; }
    bool valid = true;
    for (size_t j = 0; j < extra; ++j) {
      unsigned char next = static_cast<unsigned char>(s[i + j]);
      if ((next & 0xc0) != 0x80) { valid = false; break; }
      cp = (cp << 6) | (next & 0x3f);
    }
    const unsigned minimum = extra == 1 ? 0x80 : extra == 2 ? 0x800 : 0x10000;
    if (!valid || cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
      appendCodepoint(p, 0xfffd);
      if (valid) i += extra;
      continue;
    }
    i += extra;
    appendCodepoint(p, cp);
  }
}

static void appendEntity(STMarkdownParser* p, const char* s, size_t n) {
  if (n > 3 && s[1] == '#') {
    unsigned cp = 0; size_t start = (s[2] == 'x' || s[2] == 'X') ? 3 : 2;
    const unsigned base = start == 3 ? 16 : 10;
    for (size_t i = start; i + 1 < n; ++i) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      unsigned value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
      if (value >= base || cp > 0x10ffff / base) { cp = 0xfffd; break; }
      cp = cp * base + value;
    }
    appendCodepoint(p, cp); return;
  }
  if (const ENTITY* entity = entity_lookup(s, n)) {
    appendCodepoint(p, entity->codepoints[0]);
    if (entity->codepoints[1]) appendCodepoint(p, entity->codepoints[1]);
  } else appendUtf8(p, s, n);
}

static bool equivalent(const STMarkdownRun& a, const STMarkdownRun& b) {
  return a.flags == b.flags && a.headingLevel == b.headingLevel && a.indent == b.indent &&
      ((a.url == nullptr && b.url == nullptr) || (a.url != nullptr && b.url != nullptr && std::strcmp(a.url, b.url) == 0));
}

static bool sameStyle(const STMarkdownRun& current, const STMarkdownParser::SavedRun& previous) {
  return current.flags == previous.flags && current.headingLevel == previous.heading && current.indent == previous.indent &&
      ((current.url == nullptr && previous.url.empty()) || (current.url != nullptr && previous.url == current.url));
}

static size_t changedStart(const STMarkdownParser* p) {
  if (p->previousText.empty() && p->previousRuns.empty()) return 0;
  const size_t limit = std::min(p->previousText.size(), p->text.size());
  size_t at = 0, currentRun = 0, previousRun = 0;
  while (at < limit && currentRun < p->runs.size() && previousRun < p->previousRuns.size()) {
    const auto& current = p->runs[currentRun]; const auto& previous = p->previousRuns[previousRun];
    const size_t end = std::min({limit, current.start + current.length, previous.start + previous.length});
    if (!sameStyle(current, previous)) return at;
    const auto mismatch = std::mismatch(p->text.begin() + at, p->text.begin() + end, p->previousText.begin() + at).first;
    if (mismatch != p->text.begin() + end) return static_cast<size_t>(mismatch - p->text.begin());
    at = end;
    if (at == current.start + current.length) ++currentRun;
    if (at == previous.start + previous.length) ++previousRun;
  }
  if (at == limit && p->text.size() == p->previousText.size()) return limit;
  return at;
}
static bool sameBlock(const STMarkdownBlock& current, const STMarkdownParser::SavedBlock& previous) {
  if (current.start != previous.start || current.length != previous.length ||
      current.kind != previous.kind || current.rowCount != previous.rows ||
      current.columnCount != previous.columns || (current.url ? current.url : "") != previous.url ||
      current.altLength != previous.alt.size() ||
      (current.altLength && !std::equal(current.alt, current.alt + current.altLength, previous.alt.begin()))) return false;
  const size_t count = current.rowCount * current.columnCount;
  if (previous.cells.size() != count) return false;
  for (size_t i = 0; i < count; ++i) {
    const auto& cell = current.cells[i];
    const auto& old = previous.cells[i];
    if (cell.flags != old.flags || cell.alignment != old.alignment || cell.textLength != old.text.size() ||
        (cell.textLength && !std::equal(cell.text, cell.text + cell.textLength, old.text.begin())) ||
        cell.runCount != old.runs.size()) return false;
    for (size_t j = 0; j < cell.runCount; ++j) {
      if (cell.runs[j].start != old.runs[j].start || cell.runs[j].length != old.runs[j].length ||
          !sameStyle(cell.runs[j], old.runs[j])) return false;
    }
  }
  return true;
}

static size_t changedBlockStart(const STMarkdownParser* p) {
  const size_t count = std::min(p->blocks.size(), p->previousBlocks.size());
  for (size_t i = 0; i < count; ++i) {
    if (!sameBlock(p->blocks[i], p->previousBlocks[i]))
      return std::min(p->blocks[i].start, p->previousBlocks[i].start);
  }
  if (p->blocks.size() > count) return p->blocks[count].start;
  if (p->previousBlocks.size() > count) return p->previousBlocks[count].start;
  return static_cast<size_t>(-1);
}

static STMarkdownParser::SavedRun saveRun(const STMarkdownRun& run) {
  return {run.start, run.length, run.flags, run.headingLevel, run.indent, run.url ? run.url : ""};
}

static void saveDocument(STMarkdownParser* p, bool blocksChanged) {
  p->previousText = p->text;
  p->previousRuns.resize(p->runs.size());
  for (size_t i = 0; i < p->runs.size(); ++i) p->previousRuns[i] = saveRun(p->runs[i]);
  if (!blocksChanged) return;
  // Reuse cell/text/run capacity across snapshots. An unchanged rich prefix needs no copy.
  p->previousBlocks.resize(p->blocks.size());
  for (size_t i = 0; i < p->blocks.size(); ++i) {
    const auto& block = p->blocks[i];
    auto& copy = p->previousBlocks[i];
    copy.start = block.start; copy.length = block.length; copy.kind = block.kind;
    copy.rows = block.rowCount; copy.columns = block.columnCount;
    copy.url = block.url ? block.url : "";
    if (block.altLength) copy.alt.assign(block.alt, block.alt + block.altLength); else copy.alt.clear();
    copy.cells.resize(block.rowCount * block.columnCount);
    for (size_t j = 0; j < copy.cells.size(); ++j) {
      const auto& cell = block.cells[j];
      auto& saved = copy.cells[j];
      saved.flags = cell.flags; saved.alignment = cell.alignment;
      if (cell.textLength) saved.text.assign(cell.text, cell.text + cell.textLength); else saved.text.clear();
      saved.runs.resize(cell.runCount);
      for (size_t k = 0; k < cell.runCount; ++k) saved.runs[k] = saveRun(cell.runs[k]);
    }
  }
}

static void appendRun(STMarkdownParser* p, size_t start, size_t length) {
  if (length == 0) return;
  STMarkdownRun run{start, length, p->flags, p->heading, p->listDepth, nullptr};
  if ((p->flags & STMD_RUN_LINK) != 0 && !p->urls.empty()) {
    p->runUrls.push_back(p->urls.back());
    run.url = p->runUrls.back().c_str();
  }
  if (!p->runs.empty() && p->runs.back().start + p->runs.back().length == start && equivalent(p->runs.back(), run)) {
    p->runs.back().length += length;
  } else p->runs.push_back(run);
}

static void appendText(STMarkdownParser* p, const char* s, size_t n, bool entity = false) {
  if (p->tableDepth && p->activeTable < p->tableOwned.size() && p->activeCell < p->tableOwned[p->activeTable].size()) {
    auto& cell = p->tableOwned[p->activeTable][p->activeCell];
    const size_t start = cell.text.size();
    auto saved = std::move(p->text); p->text = std::move(cell.text);
    if (entity) appendEntity(p, s, n); else appendUtf8(p, s, n);
    cell.text = std::move(p->text); p->text = std::move(saved);
    if (cell.text.size() > start) {
      const char* url = nullptr;
      if ((p->flags & STMD_RUN_LINK) && !p->urls.empty()) {
        p->runUrls.push_back(p->urls.back()); url = p->runUrls.back().c_str();
      }
      STMarkdownRun run{start, cell.text.size() - start, p->flags, p->heading, p->listDepth, url};
      if (!cell.runs.empty() && equivalent(cell.runs.back(), run)) cell.runs.back().length += run.length;
      else cell.runs.push_back(run);
    }
    return;
  }
  const size_t start = p->text.size();
  if (entity) appendEntity(p, s, n); else appendUtf8(p, s, n);
  appendRun(p, start, p->text.size() - start);
}
static void newline(STMarkdownParser* p) {
  if (!p->text.empty() && p->text.back() != '\n') appendText(p, "\n", 1);
}
static void highlightCode(STMarkdownParser* p, size_t start, size_t end, size_t runStart, stmd::Language language) {
  if (language == stmd::Language::none || end <= start) return;
  const auto tokens = stmd::syntaxTokens(p->text.data() + start, end - start, language);
  if (tokens.empty()) return;
  std::vector<STMarkdownRun> tail;
  tail.reserve(p->runs.size() - runStart + tokens.size());
  size_t tokenIndex = 0;
  for (size_t runIndex = runStart; runIndex < p->runs.size(); ++runIndex) {
    const auto& run = p->runs[runIndex];
    const size_t runEnd = run.start + run.length;
    if (runEnd <= start || run.start >= end) { tail.push_back(run); continue; }
    while (tokenIndex < tokens.size() && start + tokens[tokenIndex].start + tokens[tokenIndex].length <= run.start) ++tokenIndex;
    size_t pos = run.start, index = tokenIndex;
    while (pos < runEnd) {
      size_t next = runEnd;
      uint32_t flags = 0;
      if (index < tokens.size()) {
        const auto& token = tokens[index];
        const size_t tokenStart = start + token.start;
        const size_t tokenEnd = tokenStart + token.length;
        if (tokenStart <= pos && pos < tokenEnd) { next = std::min(runEnd, tokenEnd); flags = token.flags; }
        else if (tokenStart > pos) next = std::min(runEnd, tokenStart);
      }
      STMarkdownRun piece = run;
      piece.start = pos; piece.length = next - pos; piece.flags |= flags;
      if (!tail.empty() && tail.back().start + tail.back().length == pos && equivalent(tail.back(), piece))
        tail.back().length += piece.length;
      else tail.push_back(piece);
      pos = next;
      if (index < tokens.size() && pos >= start + tokens[index].start + tokens[index].length) ++index;
    }
    tokenIndex=index;
  }
  p->runs.resize(runStart); p->runs.insert(p->runs.end(), tail.begin(), tail.end());
}

static std::string attribute(const MD_ATTRIBUTE* a) {
  if (!a || !a->text) return {};
  std::string out;
  for (size_t part = 0; a->substr_offsets[part] < a->size; ++part) {
    const size_t begin = a->substr_offsets[part], end = a->substr_offsets[part + 1];
    if (a->substr_types[part] != MD_TEXT_ENTITY) { out.append(a->text + begin, end - begin); continue; }
    const char* s = a->text + begin; const size_t n = end - begin;
    if (n > 3 && s[1] == '#') {
      unsigned cp = 0; const size_t start = (s[2] == 'x' || s[2] == 'X') ? 3 : 2; const unsigned base = start == 3 ? 16 : 10;
      for (size_t i = start; i + 1 < n; ++i) { const char c = s[i]; const unsigned v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base; if (v >= base || cp > 0x10ffff / base) { cp = 0xfffd; break; } cp = cp * base + v; }
      appendUtf8Codepoint(out, cp);
    } else if (const ENTITY* entity = entity_lookup(s, n)) { appendUtf8Codepoint(out, entity->codepoints[0]); if (entity->codepoints[1]) appendUtf8Codepoint(out, entity->codepoints[1]); }
    else out.append(s, n);
  }
  return out;
}

static int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* p = static_cast<STMarkdownParser*>(userdata);
  switch (type) {
    case MD_BLOCK_QUOTE: ++p->quoteDepth; p->flags |= STMD_RUN_QUOTE; break;
    case MD_BLOCK_UL: ++p->listDepth; p->lists.push_back({false, 0}); break;
    case MD_BLOCK_OL: {
      ++p->listDepth;
      const auto* ordered = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
      p->lists.push_back({true, ordered ? ordered->start : 1});
      break;
    }
    case MD_BLOCK_LI: {
      newline(p);
      if (p->quoteDepth) appendText(p, "> ", 2);
      if (p->listDepth > 1) appendText(p, std::string(2 * static_cast<size_t>(p->listDepth - 1), ' ').c_str(), 2 * static_cast<size_t>(p->listDepth - 1));
      auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
      if (li && li->is_task) appendText(p, li->task_mark == 'x' || li->task_mark == 'X' ? "[x] " : "[ ] ", 4);
      else if (!p->lists.empty() && p->lists.back().ordered) {
        const std::string marker = std::to_string(p->lists.back().next++) + ". ";
        appendText(p, marker.data(), marker.size());
      } else appendText(p, "• ", std::strlen("• "));
      break;
    }
    case MD_BLOCK_H: p->heading = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level); break;
    case MD_BLOCK_CODE: {
      ++p->codeDepth;
      const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
      p->codeLanguage = stmd::syntaxLanguage(attribute(code ? &code->lang : nullptr));
      p->flags |= STMD_RUN_CODE_BLOCK | STMD_RUN_CODE | codeLanguageFlag(p->codeLanguage);
      newline(p); p->codeStart = p->text.size(); p->codeRunStart = p->runs.empty() ? 0 : p->runs.size() - 1;
      break;
    }
    case MD_BLOCK_TABLE: {
      const auto* table = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
      const size_t start = p->text.size(); appendCodepoint(p, 0xfffc); appendRun(p, start, 1);
      p->blocks.push_back({start, 1, STMD_BLOCK_TABLE, 0, table ? table->col_count : 0, nullptr, nullptr, nullptr, 0});
      p->tableOwned.emplace_back(); p->tablePublic.emplace_back(); p->tableBlockIndices.push_back(p->blocks.size() - 1); p->activeTable = p->tableOwned.size() - 1;
      p->tableColumns = table ? table->col_count : 0; p->tableRows = 0; p->activeCell = static_cast<size_t>(-1); ++p->tableDepth;
      break;
    }
    case MD_BLOCK_TR: if (p->tableDepth) ++p->tableRows; else p->rowHasCell = false; break;
    case MD_BLOCK_TH: case MD_BLOCK_TD:
      if (p->tableDepth) {
        const auto* cell = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        const uint32_t flags = type == MD_BLOCK_TH ? STMD_BLOCK_CELL_HEADER : 0;
        p->tableOwned[p->activeTable].push_back({{}, {}, flags, cell ? static_cast<int>(cell->align) : STMD_ALIGN_DEFAULT});
        p->activeCell = p->tableOwned[p->activeTable].size() - 1;
        break;
      }
      if (p->rowHasCell) appendText(p, " | ", 3);
      p->rowHasCell = true; break;
    default: break;
  }
  return 0;
}
static int leaveBlock(MD_BLOCKTYPE type, void*, void* userdata) {
  auto* p = static_cast<STMarkdownParser*>(userdata);
  switch (type) {
    case MD_BLOCK_QUOTE:
      --p->quoteDepth;
      if (p->quoteDepth == 0) p->flags &= ~STMD_RUN_QUOTE;
      newline(p);
      break;
    case MD_BLOCK_UL: case MD_BLOCK_OL: --p->listDepth; if (!p->lists.empty()) p->lists.pop_back(); newline(p); break;
    case MD_BLOCK_LI: newline(p); break;
    case MD_BLOCK_H: p->heading = 0; newline(p); break;
    case MD_BLOCK_P: newline(p); break;
    case MD_BLOCK_CODE:
      if (p->codeDepth == 1) highlightCode(p, p->codeStart, p->text.size(), p->codeRunStart, p->codeLanguage);
      --p->codeDepth;
      p->flags &= ~(STMD_RUN_CODE_BLOCK | STMD_RUN_LANGUAGE_MASK);
      if (p->inlineCodeDepth == 0) p->flags &= ~STMD_RUN_CODE;
      // MD4C includes a fence body's trailing line ending in the code run.
      // Keep a plain separator so adjacent same-language blocks cannot coalesce
      // into one visual group on the platform side.
      if (!p->text.empty() && p->text.back() == '\n') appendText(p, "\n", 1);
      else newline(p);
      break;
    case MD_BLOCK_TH: case MD_BLOCK_TD:
      if (p->tableDepth) p->activeCell = static_cast<size_t>(-1);
      break;
    case MD_BLOCK_TABLE:
      if (p->tableDepth && p->activeTable < p->tableOwned.size()) {
        auto& owned = p->tableOwned[p->activeTable]; auto& exported = p->tablePublic[p->activeTable];
        exported.clear(); exported.reserve(owned.size());
        for (auto& cell : owned) exported.push_back({cell.text.empty() ? nullptr : cell.text.data(), cell.text.size(), cell.runs.empty() ? nullptr : cell.runs.data(), cell.runs.size(), cell.flags, cell.alignment});
        auto& block = p->blocks[p->tableBlockIndices[p->activeTable]]; block.rowCount = p->tableRows; block.columnCount = p->tableColumns;
        block.cells = exported.empty() ? nullptr : exported.data();
        p->activeTable = static_cast<size_t>(-1); p->activeCell = static_cast<size_t>(-1); --p->tableDepth;
      }
      break;
    case MD_BLOCK_TR: newline(p); break;
    default: break;
  }
  return 0;
}
static int enterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
  auto* p = static_cast<STMarkdownParser*>(userdata);
  switch (type) {
    case MD_SPAN_EM: ++p->italicDepth; p->flags |= STMD_RUN_ITALIC; break;
    case MD_SPAN_STRONG: ++p->boldDepth; p->flags |= STMD_RUN_BOLD; break;
    case MD_SPAN_DEL: ++p->strikeDepth; p->flags |= STMD_RUN_STRIKE; break;
    case MD_SPAN_CODE: ++p->inlineCodeDepth; p->flags |= STMD_RUN_CODE; break;
    case MD_SPAN_A: p->flags |= STMD_RUN_LINK; p->urls.push_back(attribute(&static_cast<MD_SPAN_A_DETAIL*>(detail)->href)); break;
    case MD_SPAN_IMG: {
      if (p->imageDepth > 0 || p->tableDepth > 0) { ++p->imageDepth; break; }
      const size_t start = p->text.size();
      appendCodepoint(p, 0xfffc); appendRun(p, start, 1);
      const auto* image = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      p->blockUrls.push_back(attribute(image ? &image->src : nullptr));
      p->blocks.push_back({start, 1, STMD_BLOCK_IMAGE, 0, 0, nullptr, p->blockUrls.back().c_str(), nullptr, 0});
      ++p->imageDepth; p->activeImage = p->blocks.size() - 1; p->imageAlt.clear();
      break;
    }
    default: break;
  }
  return 0;
}
static int leaveSpan(MD_SPANTYPE type, void*, void* userdata) {
  auto* p = static_cast<STMarkdownParser*>(userdata);
  switch (type) {
    case MD_SPAN_EM: if (--p->italicDepth == 0) p->flags &= ~STMD_RUN_ITALIC; break;
    case MD_SPAN_STRONG: if (--p->boldDepth == 0) p->flags &= ~STMD_RUN_BOLD; break;
    case MD_SPAN_DEL: if (--p->strikeDepth == 0) p->flags &= ~STMD_RUN_STRIKE; break;
    case MD_SPAN_CODE: if (--p->inlineCodeDepth == 0 && p->codeDepth == 0) p->flags &= ~STMD_RUN_CODE; break;
    case MD_SPAN_A: p->flags &= ~STMD_RUN_LINK; if (!p->urls.empty()) p->urls.pop_back(); break;
    case MD_SPAN_IMG:
      if (--p->imageDepth == 0 && p->activeImage < p->blocks.size()) {
        p->blockAlts.push_back(std::move(p->imageAlt));
        auto& block = p->blocks[p->activeImage];
        block.alt = p->blockAlts.back().empty() ? nullptr : p->blockAlts.back().data();
        block.altLength = p->blockAlts.back().size();
        p->activeImage = static_cast<size_t>(-1);
      }
      break;
    default: break;
  }
  return 0;
}
static int textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
  auto* p = static_cast<STMarkdownParser*>(userdata);
  if (p->imageDepth > 0 && p->tableDepth == 0) {
    // MD4C already distinguishes escaped/code text from entities: decode exactly once here.
    p->text.swap(p->imageAlt);
    if (type == MD_TEXT_ENTITY) appendEntity(p, text, size);
    else if (type == MD_TEXT_BR) appendCodepoint(p, '\n');
    else if (type == MD_TEXT_SOFTBR) appendCodepoint(p, ' ');
    else if (type == MD_TEXT_NULLCHAR) appendCodepoint(p, 0xfffd);
    else appendUtf8(p, text, size);
    p->text.swap(p->imageAlt);
    return 0;
  }
  if (type == MD_TEXT_BR) appendText(p, "\n", 1);
  else if (type == MD_TEXT_SOFTBR) appendText(p, " ", 1);
  else if (type == MD_TEXT_ENTITY) appendText(p, text, size, true);
  else if (type == MD_TEXT_NULLCHAR) appendText(p, "\xef\xbf\xbd", 3);
  else appendText(p, text, size);
  return 0;
}
} // namespace

extern "C" STMarkdownParser* stmd_create(void) { return new STMarkdownParser(); }
extern "C" void stmd_destroy(STMarkdownParser* parser) { delete parser; }
static const STMarkdownDocument* parse(
    STMarkdownParser* p, const char* utf8, size_t length, bool github,
    bool stripBom, bool progressive, bool final) {
  if (!p) return nullptr;
  p->text.clear(); p->runs.clear(); p->urls.clear(); p->runUrls.clear(); p->blocks.clear(); p->blockUrls.clear(); p->blockAlts.clear(); p->tableOwned.clear(); p->tablePublic.clear(); p->tableBlockIndices.clear(); p->tableDepth = 0; p->activeTable = p->activeCell = static_cast<size_t>(-1); p->imageDepth = 0; p->activeImage = static_cast<size_t>(-1); p->imageAlt.clear(); p->document = {};
  p->flags = 0; p->heading = 0; p->listDepth = 0; p->lists.clear(); p->quoteDepth = 0; p->codeDepth = 0; p->boldDepth = p->italicDepth = p->strikeDepth = p->inlineCodeDepth = 0; p->rowHasCell = false;
  if (length > static_cast<size_t>(UINT_MAX) || (!utf8 && length != 0)) { p->document.error = -1; return &p->document; }
  MD_PARSER parser{};
  parser.flags = github ? MD_DIALECT_GITHUB : MD_DIALECT_COMMONMARK;
  parser.enter_block = enterBlock; parser.leave_block = leaveBlock;
  parser.enter_span = enterSpan; parser.leave_span = leaveSpan; parser.text = textCallback;
  // Match the streaming UTF-8 decoder: a single leading BOM is a transport
  // marker, not visible Markdown content. Later U+FEFF code points stay text.
  const char* input = utf8 ? utf8 : "";
  std::string projection;
  if (progressive && !final && utf8 && makeStreamingProjection(utf8, length, &projection) &&
      !hasOpenFencedBlock(utf8, length)) {
    input = projection.data();
  }
  size_t inputLength = input == utf8 ? length : projection.size();
  if (stripBom && inputLength >= 3 && static_cast<unsigned char>(input[0]) == 0xef &&
      static_cast<unsigned char>(input[1]) == 0xbb && static_cast<unsigned char>(input[2]) == 0xbf) {
    input += 3;
    inputLength -= 3;
  }
  p->document.error = md_parse(input, static_cast<MD_SIZE>(inputLength), &parser, p);
  p->document.text = p->text.data(); p->document.textLength = p->text.size();
  p->document.runs = p->runs.data(); p->document.runCount = p->runs.size();
  p->document.blocks = p->blocks.data(); p->document.blockCount = p->blocks.size();
  p->document.sourceBytesConsumed = p->document.error == 0 ? length : 0;
  p->document.changedStartUTF16 = p->document.error == 0 ? changedStart(p) : 0;
  if (p->document.error == 0) {
    const size_t blockStart = changedBlockStart(p);
    p->document.changedStartUTF16 = std::min(p->document.changedStartUTF16, blockStart);
    saveDocument(p, blockStart != static_cast<size_t>(-1));
  }
  return &p->document;
}
extern "C" const STMarkdownDocument* stmd_parse(STMarkdownParser* p, const char* utf8, size_t length, bool github) {
  return parse(p, utf8, length, github, true, false, true);
}
extern "C" const STMarkdownDocument* stmd_parse_decoded(STMarkdownParser* p, const char* utf8, size_t length, bool github) {
  return parse(p, utf8, length, github, false, false, true);
}
extern "C" const STMarkdownDocument* stmd_parse_streaming(
    STMarkdownParser* p, const char* utf8, size_t length, bool github, bool final) {
  return parse(p, utf8, length, github, true, true, final);
}
extern "C" const STMarkdownDocument* stmd_parse_decoded_streaming(
    STMarkdownParser* p, const char* utf8, size_t length, bool github, bool final) {
  return parse(p, utf8, length, github, false, true, final);
}
