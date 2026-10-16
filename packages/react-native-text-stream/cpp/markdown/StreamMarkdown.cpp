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
  bool trackChanges = true;
  enum class RejectedContext { none, literalBlock };
  // Populated only after MD4C rejects a projection in a known block context.
  // Keep exact bytes: a tail hash cannot detect edits earlier in the document.
  std::string rejectedPrefix;
  RejectedContext rejectedContext = RejectedContext::none;
  bool rejectedGithub = false, rejectedStripBom = false;
  uintptr_t projectionProbe = 0;
  RejectedContext projectionContext = RejectedContext::none;
  int htmlDepth = 0;
#ifdef STMD_TESTING
  size_t parseCalls = 0;
#endif
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
  const char* pendingImageMarker = nullptr;
  bool pendingImageSeen = false;
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
  const char* unfinishedSourceEnd = nullptr;
  uintptr_t provisionalHeaderStart = 0, provisionalHeaderEnd = 0;
  bool provisionalHeaderSeen = false;
  int boldDepth = 0, italicDepth = 0, strikeDepth = 0, inlineCodeDepth = 0;
  bool rowHasCell = false;
  std::vector<uint16_t> previousText;
  std::vector<SavedRun> previousRuns;
  std::vector<SavedBlock> previousBlocks;
};

namespace {

constexpr size_t kStreamingTailBytes = 8192;
constexpr char kStreamingImageMarker[] = "streamtext:pending";

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

static bool hasOpenFencedBlock(const char* input, size_t length);

static bool isEscapedAt(const char* input, size_t at) {
  size_t slashes = 0;
  while (at > slashes && input[at - slashes - 1] == '\\') ++slashes;
  return (slashes & 1u) != 0;
}

static size_t tableColumnCount(const char* input, size_t start, size_t end) {
  while (start < end && isSpace(input[start])) ++start;
  while (end > start && isSpace(input[end - 1])) --end;
  if (start < end && input[start] == '|') ++start;
  if (end > start && input[end - 1] == '|') --end;
  if (start == end) return 0;
  size_t count = 1;
  for (size_t i = start; i < end; ++i) if (input[i] == '|' && !isEscapedAt(input, i)) ++count;
  return count;
}

// Returns true only for the line shape that starts a GFM table. Requiring the
// leading pipe is intentional: a pipe in ordinary prose must not temporarily
// turn the paragraph into a grid while it is still being typed. The delimiter
// remains the final authority; this is only a reversible preview.
static bool isProvisionalTableHeader(const char* input, size_t start, size_t end) {
  size_t indent = 0;
  while (start < end && input[start] == ' ' && indent < 4) { ++start; ++indent; }
  // Four spaces begin an indented code block, never a table.
  if (indent == 4) return false;
  if (start >= end || input[start] != '|' || isEscapedAt(input, start)) return false;
  ++start;
  while (start < end && isSpace(input[start])) ++start;
  // A bare pipe is not yet enough evidence to reserve an accessible table
  // cell. Publish as soon as the first readable cell byte arrives.
  return start < end;
}

static bool hasConfirmedLeadingPipeTableTail(const char* input, size_t length);

static bool followsPipeRow(const char* input, size_t headerStart, size_t lowerBound) {
  if (headerStart == 0) return false;
  size_t previousStart = 0;
  return previousLineStart(input, headerStart, lowerBound, &previousStart) &&
      isProvisionalTableHeader(input, previousStart, lineEnd(input, headerStart));
}

static void appendProvisionalTableDelimiter(std::string* output, size_t columns) {
  output->append("\n|");
  for (size_t column = 0; column < columns; ++column) output->append(" --- |");
}

// MD4C needs a complete delimiter before it emits a table. While the model is
// writing the header or delimiter, give MD4C a private, complete delimiter so
// native renderers receive the same table block/cells they use after it is
// confirmed. The projection is never exposed as source: sourceBytesConsumed
// still covers the caller's exact bytes, and the next parse falls back to the
// literal document if the candidate ceases to be a table.
static bool makeProvisionalTableProjection(const char* input, size_t length, std::string* output, size_t* expectedColumns, size_t* headerOffset, size_t* headerLimit) {
  if (length == 0) return false;
  *expectedColumns = 0;
  if (hasConfirmedLeadingPipeTableTail(input, length)) return false;
  const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
  size_t currentStart = length;
  while (currentStart > boundedStart && input[currentStart - 1] != '\n') --currentStart;
  if (currentStart == boundedStart && boundedStart != 0 && input[currentStart - 1] != '\n') return false;
  const size_t currentEnd = lineEnd(input, length);
  // lineEnd excludes a trailing newline, while currentStart then points just
  // after it. Keep a just-finished candidate header visible while the next
  // delimiter line has not produced its first byte yet.
  if (currentEnd < currentStart) {
    size_t headerStart = 0;
    if (!previousLineStart(input, currentStart, boundedStart, &headerStart) ||
        !isProvisionalTableHeader(input, headerStart, lineEnd(input, currentStart))) return false;
    if (followsPipeRow(input, headerStart, boundedStart)) return false;
    const size_t columns = tableColumnCount(input, headerStart, lineEnd(input, currentStart));
    if (columns == 0) return false;
    output->assign(input, length - 1);
    *headerOffset = headerStart; *headerLimit = output->size();
    appendProvisionalTableDelimiter(output, columns);
    *expectedColumns = columns;
    return true;
  }

  // The next line can be the still-incomplete delimiter. Preserve the header
  // table while its dashes arrive, but only for delimiter characters allowed
  // by GFM. Any other byte immediately restores the literal source.
  if (currentStart != 0 && !isCompleteTableDelimiter(input, currentStart, currentEnd) &&
      isPotentialTableDelimiter(input, currentStart, currentEnd)) {
    size_t headerStart = 0;
    if (previousLineStart(input, currentStart, boundedStart, &headerStart) &&
        isProvisionalTableHeader(input, headerStart, lineEnd(input, currentStart))) {
      if (followsPipeRow(input, headerStart, boundedStart)) return false;
      const size_t columns = tableColumnCount(input, headerStart, lineEnd(input, currentStart));
      if (columns == 0) return false;
      // Drop the source newline before appending our delimiter; otherwise the
      // blank line would terminate the header paragraph before MD4C sees it.
      output->assign(input, currentStart - 1);
      *headerOffset = headerStart; *headerLimit = output->size();
      appendProvisionalTableDelimiter(output, columns);
      *expectedColumns = columns;
      return true;
    }
  }

  // The actively growing header: "| Name" already renders as a one-cell
  // table; each following pipe/cell updates the same provisional block.
  // Once a real delimiter precedes this line, it is a data row, not a new
  // provisional header. Let the normal MD4C table path publish it.
  if (currentStart != 0) {
    size_t previousStart = 0;
    if (previousLineStart(input, currentStart, boundedStart, &previousStart) &&
        isCompleteTableDelimiter(input, previousStart, lineEnd(input, currentStart))) return false;
  }
  if (!isProvisionalTableHeader(input, currentStart, currentEnd)) return false;
  // A header cannot start in the middle of consecutive pipe rows. This also
  // avoids a failed projection plus a second full parse when a long table's
  // real delimiter lies outside the bounded tail window.
  if (followsPipeRow(input, currentStart, boundedStart)) return false;
  const size_t columns = tableColumnCount(input, currentStart, currentEnd);
  if (columns == 0) return false;
  output->assign(input, length);
  *headerOffset = currentStart; *headerLimit = output->size();
  appendProvisionalTableDelimiter(output, columns);
  *expectedColumns = columns;
  return true;
}

// Finds an image which starts on the active line but has not completed. This
// deliberately handles nested brackets in alt text; the generic inline tail
// scanner only needs the innermost bracket for styling projections.
static bool unfinishedImage(const char* input, size_t start, size_t end, size_t* opener, size_t* labelEnd) {
  bool escaped = false;
  size_t codeTicks = 0;
  for (size_t i = start; i + 1 < end; ++i) {
    const char c = input[i];
    if (escaped) { escaped = false; continue; }
    if (c == '\\') { escaped = true; continue; }
    if (c == '`') {
      size_t count = 1; while (i + count < end && input[i + count] == '`') ++count;
      if (codeTicks == 0) codeTicks = count;
      else if (codeTicks == count) codeTicks = 0;
      i += count - 1;
      continue;
    }
    if (codeTicks != 0 || c != '!' || input[i + 1] != '[') continue;
    size_t depth = 1, at = i + 2;
    for (; at < end && depth != 0; ++at) {
      if (input[at] == '\\') { ++at; continue; }
      if (input[at] == '[') ++depth;
      else if (input[at] == ']') --depth;
    }
    if (depth != 0) { *opener = i; *labelEnd = end; return true; }
    const size_t close = at - 1;
    // At the transport boundary `![label]` can still grow into an image.
    // Once another byte follows it is prose or a reference image; keep it
    // intact and let MD4C resolve it.
    if (at == end) { *opener = i; *labelEnd = close; return true; }
    if (input[at] != '(') { i = close; continue; }
    size_t parens = 1;
    ++at;
    for (; at < end && parens != 0; ++at) {
      if (input[at] == '\\') { ++at; continue; }
      if (input[at] == '(') ++parens;
      else if (input[at] == ')') --parens;
    }
    if (parens != 0) { *opener = i; *labelEnd = close; return true; }
    i = at - 1;
  }
  return false;
}

// A resolved reference image can end exactly at a transport boundary. Do not
// send its trailing `[ref]` through the generic unfinished-link projection.
static bool endsWithReferenceImage(const char* input, size_t start, size_t end) {
  if (end <= start || input[end - 1] != ']') return false;
  bool escaped = false;
  size_t codeTicks = 0;
  for (size_t i = start; i + 1 < end; ++i) {
    const char c = input[i];
    if (escaped) { escaped = false; continue; }
    if (c == '\\') { escaped = true; continue; }
    if (c == '`') {
      size_t count = 1; while (i + count < end && input[i + count] == '`') ++count;
      if (codeTicks == 0) codeTicks = count;
      else if (codeTicks == count) codeTicks = 0;
      i += count - 1;
      continue;
    }
    if (codeTicks != 0 || c != '!' || input[i + 1] != '[') continue;
    size_t depth = 1, at = i + 2;
    for (; at < end && depth != 0; ++at) {
      if (input[at] == '\\') { ++at; continue; }
      if (input[at] == '[') ++depth;
      else if (input[at] == ']') --depth;
    }
    if (depth == 0 && at < end && input[at] == '[' && end - 1 >= at && input[end - 1] == ']') return true;
  }
  return false;
}

// MD4C recognizes a GFM table as soon as its delimiter is complete. The old
// projection hid that exact prefix in anticipation of a body row; bypass it
// and let MD4C publish the real header and partial first row directly.
static bool hasEarlyTable(const char* input, size_t length) {
  if (length == 0) return false;
  const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
  size_t currentStart = length;
  while (currentStart > boundedStart && input[currentStart - 1] != '\n') --currentStart;
  if (currentStart == boundedStart && boundedStart != 0 && input[currentStart - 1] != '\n') return false;

  size_t delimiterStart = currentStart;
  bool headerOnly = true;
  if (!isCompleteTableDelimiter(input, delimiterStart, lineEnd(input, length))) {
    if (delimiterStart == 0 || !previousLineStart(input, delimiterStart, boundedStart, &delimiterStart) ||
        !isCompleteTableDelimiter(input, delimiterStart, lineEnd(input, currentStart))) return false;
    headerOnly = currentStart == length;
  }
  size_t headerStart = 0;
  if (delimiterStart == 0 || !previousLineStart(input, delimiterStart, boundedStart, &headerStart) ||
      std::memchr(input + headerStart, '|', delimiterStart - headerStart) == nullptr) return false;
  if (tableColumnCount(input, headerStart, lineEnd(input, delimiterStart)) !=
      tableColumnCount(input, delimiterStart, lineEnd(input, headerOnly ? length : currentStart))) return false;

  return true;
}

static bool hasConfirmedLeadingPipeTableTail(const char* input, size_t length) {
  if (length == 0) return false;
  const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
  size_t start = length;
  while (start > boundedStart && input[start - 1] != '\n') --start;
  if (start == boundedStart && boundedStart != 0 && input[start - 1] != '\n') return false;
  size_t end = length;
  for (;;) {
    const size_t rowEnd = lineEnd(input, end);
    if (rowEnd < start) {
      const size_t oldStart = start;
      if (oldStart == 0 || !previousLineStart(input, oldStart, boundedStart, &start)) return false;
      end = oldStart;
      continue;
    }
    if (isCompleteTableDelimiter(input, start, rowEnd)) {
      size_t headerStart = 0;
      return previousLineStart(input, start, boundedStart, &headerStart) &&
          isProvisionalTableHeader(input, headerStart, lineEnd(input, start)) &&
          tableColumnCount(input, headerStart, lineEnd(input, start)) == tableColumnCount(input, start, rowEnd);
    }
    if (!isProvisionalTableHeader(input, start, rowEnd)) return false;
    const size_t oldStart = start;
    if (oldStart == 0 || !previousLineStart(input, oldStart, boundedStart, &start)) return false;
    end = oldStart;
  }
}

// Complete only the grammar around a final, unfinished image. MD4C then
// gives us its normal alt-text decoding and U+FFFC placement. The fabricated
// URL is discarded before publication, so it can never reach an image loader.
// Do not rescan the whole document for fences here: MD4C acceptance below is
// authoritative and reparses exact source before publication when this tail
// was actually literal fenced/HTML content.
static bool makePendingImageProjection(const char* input, size_t length, std::string* output, size_t* opener) {
  if (length == 0) return false;
  const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
  size_t lineStart = length;
  while (lineStart > boundedStart && input[lineStart - 1] != '\n') --lineStart;
  if (lineStart == boundedStart && boundedStart != 0 && input[lineStart - 1] != '\n') return false;
  size_t imageStart = 0, labelEnd = 0;
  if (!unfinishedImage(input, lineStart, length, &imageStart, &labelEnd) ||
      imageStart >= length || isEscapedAt(input, imageStart)) return false;
  const size_t labelStart = imageStart + 2; // "!["
  if (labelStart > length) return false;
  output->assign(input, imageStart);
  output->append("![", 2);
  output->append(input + labelStart, labelEnd - labelStart);
  if (labelEnd > labelStart && isEscapedAt(input, labelEnd)) output->push_back('\\');
  output->append("](");
  output->append(kStreamingImageMarker);
  output->push_back(')');
  *opener = imageStart;
  return true;
}

// Once MD4C has classified the candidate's physical line as literal block
// content (HTML/indented code), appending bytes cannot turn that same line into
// a Markdown table/image. Even an HTML closing token consumes its entire line.
static bool rejectedProjectionApplies(STMarkdownParser* p, const char* input, size_t length,
                                     bool github, bool stripBom, bool progressive, bool final) {
  using Context = STMarkdownParser::RejectedContext;
  if (p->rejectedContext == Context::none) return false;
  const size_t prefixLength = p->rejectedPrefix.size();
  if (!progressive || final || !input || github != p->rejectedGithub || stripBom != p->rejectedStripBom ||
      length < prefixLength || std::memcmp(input, p->rejectedPrefix.data(), prefixLength) != 0 ||
      std::memchr(input + prefixLength, '\n', length - prefixLength) ||
      std::memchr(input + prefixLength, '\r', length - prefixLength)) {
    p->rejectedPrefix.clear();
    p->rejectedContext = Context::none;
    return false;
  }
  return p->rejectedContext == Context::literalBlock;
}

static void rememberRejectedProjection(STMarkdownParser* p, const char* input, size_t length,
                                      size_t probeOffset, STMarkdownParser::RejectedContext context,
                                      bool github, bool stripBom) {
  if (context == STMarkdownParser::RejectedContext::none || probeOffset >= length) return;
  // A provisional delimiter can target the previous header line. Do not carry
  // that line's block classification onto a different physical line.
  if (std::memchr(input + probeOffset, '\n', length - probeOffset) ||
      std::memchr(input + probeOffset, '\r', length - probeOffset)) return;
  p->rejectedPrefix.assign(input, length);
  p->rejectedContext = context;
  p->rejectedGithub = github;
  p->rejectedStripBom = stripBom;
}

// This is deliberately a single forward pass over at most one final line.
// It never walks backward over escape runs, which keeps adversarial backslash
// tails linear. Fenced blocks are protected by a parse probe below.
static TailProjection unfinishedInline(const char* input, size_t start, size_t end, bool github) {
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
    if (github && i + 1 < end && c == '~' && input[i + 1] == '~') {
      const bool previousIsSpace = i == start || isSpace(input[i - 1]);
      const bool atBoundary = i + 2 == end;
      const bool nextIsSpace = !atBoundary && isSpace(input[i + 2]);
      if (atBoundary) finalBytePartOfPair = true;
      // A closer may be followed by prose whitespace; only an *opening*
      // delimiter needs a readable byte on its right. This also leaves
      // literal "~~ " source untouched when no span is open.
      if (strike != none && !previousIsSpace) strike = none;
      else if (atBoundary || !nextIsSpace) {
        strike = i;
      }
      ++i;
      continue;
    }
    if (github && c == '~' && strike != none && i + 1 == end) { strikePendingClose = true; continue; }
    if (i + 1 < end && (c == '*' || c == '_') && input[i + 1] == c) {
      const bool previousIsSpace = i == start || isSpace(input[i - 1]);
      const bool atBoundary = i + 2 == end;
      const bool nextIsSpace = !atBoundary && isSpace(input[i + 2]);
      if (atBoundary) finalBytePartOfPair = true;
      if (strong != none && input[strong] == c && !previousIsSpace) strong = none;
      else if (atBoundary || !nextIsSpace) {
        strong = i;
      }
      ++i;
      continue;
    }
    if (c == '*' || c == '_') {
      const bool previousIsSpace = i == start || isSpace(input[i - 1]);
      const bool atBoundary = i + 1 == end;
      const bool nextIsSpace = !atBoundary && isSpace(input[i + 1]);
      // The first byte of a strong closer is common in a byte stream. Treat
      // it as a pending strong close, not a nested emphasis opener.
      if (strong != none && c == input[strong] && atBoundary) { strongPendingClose = true; continue; }
      if (emphasis != none && c == emphasisMarker && !previousIsSpace) {
        if (atBoundary) finalBytePartOfPair = true;
        emphasis = none;
        emphasisMarker = 0;
      }
      else if ((atBoundary || !nextIsSpace) &&
          (c != '_' || i == start || !std::isalnum(static_cast<unsigned char>(input[i - 1])))) {
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
        (input[end - 1] == '*' || (github && input[end - 1] == '~'))) {
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

static bool makeStreamingProjection(const char* input, size_t length, bool github, std::string* output,
                                    size_t* projectionProbeOffset) {
  *projectionProbeOffset = static_cast<size_t>(-1);
  const auto retainedProbeBefore = [input](size_t offset) {
    while (offset > 0 && (isSpace(input[offset - 1]) || input[offset - 1] == '\n')) --offset;
    return offset == 0 ? static_cast<size_t>(-1) : offset - 1;
  };
  if (length == 0) return false;
  const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
  size_t currentStart = length;
  while (currentStart > boundedStart && input[currentStart - 1] != '\n') --currentStart;
  // A megabyte-long line is intentionally left to the final parser rather
  // than making the progressive tail scan or projection unbounded.
  if (currentStart == boundedStart && boundedStart != 0 && input[currentStart - 1] != '\n') return false;
  if (length > currentStart && input[length - 1] == '!' &&
      (length == currentStart + 1 || input[length - 2] != '\\')) {
    output->assign(input, length - 1);
    *projectionProbeOffset = retainedProbeBefore(length - 1);
    return true;
  }
  const TailProjection tail = unfinishedInline(input, currentStart, length, github);
  if (tail.kind == TailKind::none) return false;
  output->assign(input, tail.start);
  const size_t retainedProbe = retainedProbeBefore(tail.start);
  const size_t openerWidth = (input[tail.start] == '*' || input[tail.start] == '_' || input[tail.start] == '~') &&
      tail.start + 1 < length && input[tail.start + 1] == input[tail.start] ? 2 : 1;
  // A delimiter by itself has no readable payload. Withhold it rather than
  // manufacturing a visible doubled marker.
  if (tail.start + openerWidth == length) {
    *projectionProbeOffset = retainedProbe;
    return true;
  }
  if (tail.kind == TailKind::strong || tail.kind == TailKind::strike || tail.kind == TailKind::emphasis) {
    // A Markdown closing delimiter cannot follow whitespace. Preserve a
    // streamed trailing space outside the provisional styled span instead.
    size_t payloadEnd = length;
    while (payloadEnd > tail.start && isSpace(input[payloadEnd - 1])) --payloadEnd;
    output->append(input + tail.start, payloadEnd - tail.start);
    *projectionProbeOffset = output->size();
    for (size_t i = 0; i < tail.closerCount; ++i) output->append(tail.widths[i], tail.markers[i]);
    output->append(input + payloadEnd, length - payloadEnd);
  } else if (tail.kind == TailKind::code) {
    output->append(input + tail.start, length - tail.start);
    *projectionProbeOffset = output->size();
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
    *projectionProbeOffset = output->size() > tail.start ? tail.start : retainedProbe;
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
    size_t at = start, indent = 0;
    while (at < end && input[at] == ' ' && indent < 4) { ++at; ++indent; }
    if (indent == 4) { start = end == length ? length : end + 1; continue; }
    if (at < end && input[at] == '>') { ++at; if (at < end && input[at] == ' ') ++at; }
    size_t count = 0;
    const char c = at < end ? input[at] : 0;
    while (at + count < end && input[at + count] == c) ++count;
    if ((c == '`' || c == '~') && count >= 3) {
      bool tailIsSpaces = true;
      for (size_t tail = at + count; tail < end; ++tail) {
        if (input[tail] != ' ' && input[tail] != '\t' && input[tail] != '\r') { tailIsSpaces = false; break; }
      }
      if (marker == 0) {
        // Backtick fence info strings cannot contain a backtick. Without this
        // guard a single inline-code line disables every tail projection.
        if (c == '`' && !tailIsSpaces) {
          bool hasTick = false;
          for (size_t tail = at + count; tail < end; ++tail) if (input[tail] == '`') { hasTick = true; break; }
          if (hasTick) { start = end == length ? length : end + 1; continue; }
        }
        marker = c; markerCount = count;
      } else if (marker == c && count >= markerCount && tailIsSpaces) { marker = 0; markerCount = 0; }
    }
    start = end == length ? length : end + 1;
  }
  return marker != 0;
}

// An opener-only projection can leave an indented code line with no text
// callback to probe. Tabs count in visual columns, as they do in Markdown.
static bool hasIndentedLiteralTail(const char* input, size_t length) {
  size_t lineStart = length;
  while (lineStart > 0 && input[lineStart - 1] != '\n') --lineStart;
  size_t columns = 0;
  for (size_t at = lineStart; at < length && (input[at] == ' ' || input[at] == '\t'); ++at) {
    columns += input[at] == '\t' ? 4 - columns % 4 : 1;
    if (columns >= 4) return true;
  }
  return false;
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
    case MD_BLOCK_HTML: ++p->htmlDepth; break;
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
      if (li && li->is_task) {
        const uint32_t savedFlags = p->flags;
        p->flags |= STMD_RUN_TASK_MARKER;
        if (li->task_mark == 'x' || li->task_mark == 'X') p->flags |= STMD_RUN_TASK_CHECKED;
        appendText(p, li->task_mark == 'x' || li->task_mark == 'X' ? "[x] " : "[ ] ", 4);
        p->flags = savedFlags;
      }
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
      const bool unfinishedLanguage = code && code->lang.text && code->lang.size != 0 &&
          code->lang.text + code->lang.size == p->unfinishedSourceEnd;
      p->codeLanguage = unfinishedLanguage ? stmd::Language::none :
          stmd::syntaxLanguage(attribute(code ? &code->lang : nullptr));
      newline(p);
      p->flags |= STMD_RUN_CODE_BLOCK | STMD_RUN_CODE | codeLanguageFlag(p->codeLanguage);
      p->codeStart = p->text.size(); p->codeRunStart = p->runs.empty() ? 0 : p->runs.size() - 1;
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
    case MD_BLOCK_HTML: --p->htmlDepth; break;
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
      // MD4C correctly reports an unfinished/empty fenced block even at a
      // transport boundary, but it has no text callback for its body. Publish
      // one styled line while CODE_BLOCK is still active so native renderers
      // can draw the block surface and language header immediately.
      if (p->codeDepth == 1 && p->text.size() == p->codeStart) appendText(p, "\n", 1);
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
      if (p->imageDepth > 0 || p->tableDepth > 0) {
        const auto* image = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        if (p->tableDepth > 0 && image && image->src.text == p->pendingImageMarker) {
          // Tables currently expose rich cell text rather than nested image
          // attachments. Accept the private projection so MD4C publishes the
          // safe alt label instead of falling back to raw ![alt](url) syntax.
          p->pendingImageSeen = true;
        }
        ++p->imageDepth; break;
      }
      const size_t start = p->text.size();
      appendCodepoint(p, 0xfffc); appendRun(p, start, 1);
      const auto* image = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      const bool pending = image && image->src.text == p->pendingImageMarker &&
          image->src.size == std::strlen(kStreamingImageMarker);
      if (pending) {
        p->blocks.push_back({start, 1, STMD_BLOCK_IMAGE_PENDING, 0, 0, nullptr, nullptr, nullptr, 0});
        p->pendingImageSeen = true;
      } else {
        p->blockUrls.push_back(attribute(image ? &image->src : nullptr));
        p->blocks.push_back({start, 1, STMD_BLOCK_IMAGE, 0, 0, nullptr, p->blockUrls.back().c_str(), nullptr, 0});
      }
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
  if (p->projectionProbe != 0 && size != 0 && (p->codeDepth > 0 || p->htmlDepth > 0)) {
    const auto address = reinterpret_cast<uintptr_t>(text);
    if (address <= p->projectionProbe && p->projectionProbe - address < size)
      p->projectionContext = STMarkdownParser::RejectedContext::literalBlock;
  }
  // Accept a preview only if MD4C consumed THIS header as table cell text.
  // An unrelated earlier table must not authorize synthetic syntax in HTML/code.
  if (p->tableDepth > 0 && p->provisionalHeaderStart != 0 && size != 0) {
    const auto address = reinterpret_cast<uintptr_t>(text);
    if (address >= p->provisionalHeaderStart && address < p->provisionalHeaderEnd)
      p->provisionalHeaderSeen = true;
  }
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
extern "C" void stmd_set_track_changes(STMarkdownParser* parser, bool enabled) {
  if (!parser) return;
  parser->trackChanges = enabled;
  if (!enabled) {
    parser->previousText.clear();
    parser->previousRuns.clear();
    parser->previousBlocks.clear();
  }
}
#ifdef STMD_TESTING
extern "C" size_t stmd_test_parse_calls(const STMarkdownParser* parser) { return parser->parseCalls; }
#endif
static const STMarkdownDocument* parse(
    STMarkdownParser* p, const char* utf8, size_t length, bool github,
    bool stripBom, bool progressive, bool final) {
  if (!p) return nullptr;
  p->projectionProbe = 0;
  p->projectionContext = STMarkdownParser::RejectedContext::none;
  p->htmlDepth = 0;
  p->text.clear(); p->runs.clear(); p->urls.clear(); p->runUrls.clear(); p->blocks.clear(); p->blockUrls.clear(); p->blockAlts.clear(); p->tableOwned.clear(); p->tablePublic.clear(); p->tableBlockIndices.clear(); p->tableDepth = 0; p->activeTable = p->activeCell = static_cast<size_t>(-1); p->imageDepth = 0; p->activeImage = static_cast<size_t>(-1); p->imageAlt.clear(); p->pendingImageMarker = nullptr; p->pendingImageSeen = false; p->document = {};
  p->flags = 0; p->heading = 0; p->listDepth = 0; p->lists.clear(); p->quoteDepth = 0; p->codeDepth = 0; p->boldDepth = p->italicDepth = p->strikeDepth = p->inlineCodeDepth = 0; p->rowHasCell = false;
  if (length > static_cast<size_t>(UINT_MAX) || (!utf8 && length != 0)) { p->document.error = -1; return &p->document; }
  const bool skipRejectedProjection = rejectedProjectionApplies(p, utf8, length, github, stripBom, progressive, final);
  p->unfinishedSourceEnd = nullptr;
  p->provisionalHeaderStart = p->provisionalHeaderEnd = 0;
  p->provisionalHeaderSeen = false;
  MD_PARSER parser{};
  parser.flags = github ? MD_DIALECT_GITHUB : MD_DIALECT_COMMONMARK;
  parser.enter_block = enterBlock; parser.leave_block = leaveBlock;
  parser.enter_span = enterSpan; parser.leave_span = leaveSpan; parser.text = textCallback;
  // Match the streaming UTF-8 decoder: a single leading BOM is a transport
  // marker, not visible Markdown content. Later U+FEFF code points stay text.
  const char* input = utf8 ? utf8 : "";
  std::string projection;
  bool pendingImage = false;
  bool provisionalTable = false;
  bool inlineProjection = false;
  size_t inlineProjectionProbeOffset = static_cast<size_t>(-1);
  size_t provisionalTableColumns = 0, headerOffset = 0, headerLimit = 0, imageOffset = 0;
  if (progressive && !final && utf8 && !skipRejectedProjection) {
    if (github && makeProvisionalTableProjection(utf8, length, &projection, &provisionalTableColumns, &headerOffset, &headerLimit) &&
               !hasOpenFencedBlock(utf8, length)) {
      // A provisional header can itself end in an unfinished image. Complete
      // that image before restoring the private table delimiter so its cell
      // has the same readable alt label as a confirmed table row.
      std::string headerImageProjection;
      size_t headerImageOffset = 0;
      if (makePendingImageProjection(utf8, headerLimit, &headerImageProjection, &headerImageOffset)) {
        projection.swap(headerImageProjection);
        headerLimit = projection.size();
        appendProvisionalTableDelimiter(&projection, provisionalTableColumns);
        pendingImage = true;
        imageOffset = headerImageOffset;
      }
      input = projection.data();
      provisionalTable = true;
    } else if (makePendingImageProjection(utf8, length, &projection, &imageOffset)) {
      pendingImage = true;
      input = projection.data();
    } else if (github && hasEarlyTable(utf8, length)) {
      // MD4C already materializes the table header at this boundary.
    } else {
      const size_t boundedStart = length > kStreamingTailBytes ? length - kStreamingTailBytes : 0;
      size_t lineStart = length;
      while (lineStart > boundedStart && utf8[lineStart - 1] != '\n') --lineStart;
      const bool referenceAtBoundary = !(lineStart == boundedStart && boundedStart != 0 && utf8[lineStart - 1] != '\n') &&
          endsWithReferenceImage(utf8, lineStart, length);
      if (!referenceAtBoundary && makeStreamingProjection(utf8, length, github, &projection,
          &inlineProjectionProbeOffset) && !hasOpenFencedBlock(utf8, length) &&
          !hasIndentedLiteralTail(utf8, length)) {
        input = projection.data();
        // Inline projections normally only remove unfinished syntax.  When
        // they append a closer, however, that closer can become literal text
        // inside an indented-code or HTML block.  Probe the manufactured byte
        // so the callbacks can reject that preview before it is published.
        inlineProjection = true;
      }
    }
  }
  size_t inputLength = input == utf8 ? length : projection.size();
  // A private projection may be longer than an otherwise valid caller buffer.
  // MD4C's MD_SIZE is narrower than size_t on 64-bit Apple targets, so never
  // truncate a fabricated suffix at the parser boundary.
  if (inputLength > static_cast<size_t>(UINT_MAX)) {
    p->document.error = -1;
    return &p->document;
  }
  if (pendingImage) {
    const size_t marker = projection.rfind(kStreamingImageMarker);
    p->pendingImageMarker = marker == std::string::npos ? nullptr : projection.data() + marker;
  }
  if (stripBom && inputLength >= 3 && static_cast<unsigned char>(input[0]) == 0xef &&
      static_cast<unsigned char>(input[1]) == 0xbb && static_cast<unsigned char>(input[2]) == 0xbf) {
    input += 3;
    inputLength -= 3;
  }
  if (progressive && !final) p->unfinishedSourceEnd = input + inputLength;
  if (provisionalTable) {
    p->provisionalHeaderStart = reinterpret_cast<uintptr_t>(projection.data() + headerOffset);
    p->provisionalHeaderEnd = reinterpret_cast<uintptr_t>(projection.data() + headerLimit);
  }
  const size_t probeOffset = provisionalTable ? headerOffset : imageOffset;
  if (provisionalTable || pendingImage)
    p->projectionProbe = reinterpret_cast<uintptr_t>(projection.data() + probeOffset);
  else if (inlineProjection && inlineProjectionProbeOffset != static_cast<size_t>(-1))
    p->projectionProbe = reinterpret_cast<uintptr_t>(projection.data() + inlineProjectionProbeOffset);
#ifdef STMD_TESTING
  ++p->parseCalls;
#endif
  p->document.error = md_parse(input, static_cast<MD_SIZE>(inputLength), &parser, p);
  if (p->document.error == 0 && provisionalTable &&
      (!p->provisionalHeaderSeen || p->blocks.empty() || p->blocks.back().kind != STMD_BLOCK_TABLE ||
       p->blocks.back().rowCount != 1 || p->blocks.back().columnCount != provisionalTableColumns)) {
    // The private delimiter was not accepted in this Markdown context (for
    // example an HTML or indented-code block). Reparse the exact caller source
    // rather than leaking any synthetic syntax into the visible document.
    const auto context = p->projectionContext;
    const auto* result = parse(p, utf8, length, github, stripBom, false, true);
    if (result->error == 0) rememberRejectedProjection(p, utf8, length, probeOffset, context, github, stripBom);
    return result;
  }
  if (p->document.error == 0 && pendingImage && !p->pendingImageSeen) {
      // The fabricated URL was not accepted as an image. Preserve literal
      // Markdown until the final parser decides.
      const auto context = p->projectionContext;
      const auto* result = parse(p, utf8, length, github, stripBom, false, true);
      if (result->error == 0) rememberRejectedProjection(p, utf8, length, probeOffset, context, github, stripBom);
      return result;
  }
  if (p->document.error == 0 && inlineProjection &&
      p->projectionContext == STMarkdownParser::RejectedContext::literalBlock) {
    // A transformed tail was consumed as literal block text. Reparse the
    // caller bytes so the preview cannot hide or add source syntax.
    const auto context = p->projectionContext;
    const auto* result = parse(p, utf8, length, github, stripBom, false, true);
    if (result->error == 0 && length != 0) {
      // For an opener-only tail the probe can be on an earlier literal line.
      // Do not cache that classification onto the active line after a blank.
      const size_t sourceProbe = inlineProjectionProbeOffset < length
          ? inlineProjectionProbeOffset : length - 1;
      rememberRejectedProjection(p, utf8, length, sourceProbe, context, github, stripBom);
    }
    return result;
  }
  p->document.text = p->text.data(); p->document.textLength = p->text.size();
  p->document.runs = p->runs.data(); p->document.runCount = p->runs.size();
  p->document.blocks = p->blocks.data(); p->document.blockCount = p->blocks.size();
  p->document.sourceBytesConsumed = p->document.error == 0 ? length : 0;
  p->document.changedStartUTF16 = p->document.error == 0 && p->trackChanges ? changedStart(p) : 0;
  if (p->document.error == 0) {
    if (p->trackChanges) {
      const size_t blockStart = changedBlockStart(p);
      p->document.changedStartUTF16 = std::min(p->document.changedStartUTF16, blockStart);
      saveDocument(p, blockStart != static_cast<size_t>(-1));
    }
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
