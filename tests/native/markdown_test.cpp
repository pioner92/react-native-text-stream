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

static std::string ascii(const STMarkdownDocument* d);
static bool has(const STMarkdownDocument* d, uint32_t flag);

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
struct CellCopy {
  std::u16string text;
  std::vector<RunCopy> runs;
  uint32_t flags;
  int alignment;
  bool operator==(const CellCopy& other) const {
    return text == other.text && runs == other.runs && flags == other.flags && alignment == other.alignment;
  }
};
struct BlockCopy {
  size_t start, length;
  uint32_t kind;
  size_t rows, columns;
  std::string url;
  std::u16string alt;
  std::vector<CellCopy> cells;
  bool operator==(const BlockCopy& other) const {
    return start == other.start && length == other.length && kind == other.kind &&
        rows == other.rows && columns == other.columns && url == other.url && alt == other.alt && cells == other.cells;
  }
};
struct DocumentCopy {
  std::u16string text;
  std::vector<RunCopy> runs;
  std::vector<BlockCopy> blocks;
};
static std::u16string copyUtf16(const uint16_t* text, size_t length) {
  if (length == 0) return {};
  assert(text);
  return {reinterpret_cast<const char16_t*>(text), length};
}
static RunCopy copyRun(const STMarkdownRun& run) {
  return {run.start, run.length, run.flags, run.headingLevel, run.indent, run.url ? run.url : ""};
}
static DocumentCopy copyDocument(const STMarkdownDocument* document) {
  assert(document && document->error == 0);
  DocumentCopy copy;
  if (document->textLength != 0) {
    assert(document->text);
    copy.text = copyUtf16(document->text, document->textLength);
  }
  for (size_t i = 0; i < document->runCount; ++i) {
    copy.runs.push_back(copyRun(document->runs[i]));
  }
  for (size_t i = 0; i < document->blockCount; ++i) {
    const auto& block = document->blocks[i];
    BlockCopy blockCopy{block.start, block.length, block.kind, block.rowCount, block.columnCount,
        block.url ? block.url : "", copyUtf16(block.alt, block.altLength), {}};
    assert(block.rowCount == 0 || block.columnCount <= std::numeric_limits<size_t>::max() / block.rowCount);
    for (size_t j = 0; j < block.rowCount * block.columnCount; ++j) {
      const auto& cell = block.cells[j];
      CellCopy cellCopy{copyUtf16(cell.text, cell.textLength), {}, cell.flags, cell.alignment};
      for (size_t k = 0; k < cell.runCount; ++k) cellCopy.runs.push_back(copyRun(cell.runs[k]));
      blockCopy.cells.push_back(std::move(cellCopy));
    }
    copy.blocks.push_back(std::move(blockCopy));
  }
  return copy;
}
static void assertSame(const DocumentCopy& a, const DocumentCopy& b) {
  assert(a.text == b.text);
  assert(a.runs == b.runs);
  assert(a.blocks == b.blocks);
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
static void applyBlockPatch(std::vector<BlockCopy>& current, const DocumentCopy& next, size_t start) {
  const auto currentSplit = std::lower_bound(current.begin(), current.end(), start,
      [](const BlockCopy& block, size_t position) { return block.start < position; });
  const auto nextSplit = std::lower_bound(next.blocks.begin(), next.blocks.end(), start,
      [](const BlockCopy& block, size_t position) { return block.start < position; });
  // Metadata before changedStart must remain valid; the changed suffix is wholly replaced.
  assert(std::equal(current.begin(), currentSplit, next.blocks.begin(), nextSplit));
  current.erase(currentSplit, current.end());
  current.insert(current.end(), nextSplit, next.blocks.end());
  assert(current == next.blocks);
}
static void assertStreamSeam(const std::string& source, bool github) {
  auto* retained = stmd_create();
  assert(retained);
  StyledCopy visible;
  std::vector<BlockCopy> visibleBlocks;
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
    applyBlockPatch(visibleBlocks, actual, retainedDoc->changedStartUTF16);
    const auto expectedStyled = styledCopy(actual);
    assert(visible.text == expectedStyled.text && visible.style == expectedStyled.style);
  }
  stmd_destroy(retained);
}

static void assertStreamingProjectionSeam(const std::string& source, bool github) {
  auto* retained = stmd_create();
  assert(retained);
  StyledCopy visible;
  std::vector<BlockCopy> visibleBlocks;
  for (size_t length = 0; length <= source.size(); ++length) {
    const auto* retainedDoc = stmd_parse_streaming(retained, source.data(), length, github, false);
    const auto actual = copyDocument(retainedDoc);
    auto* fresh = stmd_create();
    assert(fresh);
    const auto expected = copyDocument(stmd_parse_streaming(fresh, source.data(), length, github, false));
    stmd_destroy(fresh);
    assertSame(actual, expected);
    assert(retainedDoc->sourceBytesConsumed == length);
    assert(retainedDoc->changedStartUTF16 <= actual.text.size());
    applyPatch(visible, actual, retainedDoc->changedStartUTF16);
    applyBlockPatch(visibleBlocks, actual, retainedDoc->changedStartUTF16);
    const auto expectedStyled = styledCopy(actual);
    assert(visible.text == expectedStyled.text && visible.style == expectedStyled.style);
  }
  const auto final = copyDocument(stmd_parse_streaming(retained, source.data(), source.size(), github, true));
  auto* freshFinal = stmd_create();
  assert(freshFinal);
  const auto literal = copyDocument(stmd_parse(freshFinal, source.data(), source.size(), github));
  stmd_destroy(freshFinal);
  assertSame(final, literal);
  stmd_destroy(retained);
}

static void assertLiteralBlockProjectionChunks(const std::string& source, bool decoded = false) {
  for (const size_t chunk : {size_t(17), size_t(128), size_t(1024)}) {
    auto* retained = stmd_create();
    assert(retained);
    for (size_t length = 0; length < source.size();) {
      length = std::min(source.size(), length + chunk);
      const auto actual = copyDocument(decoded
          ? stmd_parse_decoded_streaming(retained, source.data(), length, true, false)
          : stmd_parse_streaming(retained, source.data(), length, true, false));
      auto* fresh = stmd_create();
      assert(fresh);
      const auto expected = copyDocument(decoded
          ? stmd_parse_decoded_streaming(fresh, source.data(), length, true, false)
          : stmd_parse_streaming(fresh, source.data(), length, true, false));
      stmd_destroy(fresh);
      assertSame(actual, expected);
    }
    const auto final = copyDocument(decoded
        ? stmd_parse_decoded_streaming(retained, source.data(), source.size(), true, true)
        : stmd_parse_streaming(retained, source.data(), source.size(), true, true));
    auto* fresh = stmd_create();
    assert(fresh);
    const auto literal = copyDocument(decoded
        ? stmd_parse_decoded(fresh, source.data(), source.size(), true)
        : stmd_parse(fresh, source.data(), source.size(), true));
    stmd_destroy(fresh);
    assertSame(final, literal);
    stmd_destroy(retained);
  }
}

static void assertMixedStreamingProjectionChunks() {
  std::string source = "\xef\xbb\xbf\xf0\x9f\x98\x80 CJK: \xe6\xbc\xa2\xe5\xad\x97\n";
  while (source.size() <= 8300) source += "ordinary streamed paragraph text with UTF-8 \xe6\xbc\xa2\n";
  source += "\n```text\n**literal**\n```\n\n| A | B |\n| --- | --- |\n| one | two |\n\n<div>\n[unfinished";
  assertLiteralBlockProjectionChunks(source);
  assertLiteralBlockProjectionChunks(source, true);
}

static void assertClosedHtmlDoesNotRejectFollowingProjection() {
  for (const std::string& prefix : {"<div>old</div>\n\n", "<pre>x</pre>\n\n"}) {
    auto* parser = stmd_create();
    assert(parser);
    const std::string opener = prefix + "**";
    const auto first = copyDocument(stmd_parse_streaming(parser, opener.data(), opener.size(), true, false));
    auto* fresh = stmd_create();
    assert(fresh);
    assertSame(first, copyDocument(stmd_parse_streaming(fresh, opener.data(), opener.size(), true, false)));
    stmd_destroy(fresh);
    const std::string payload = opener + "bold";
    const auto preview = copyDocument(stmd_parse_streaming(parser, payload.data(), payload.size(), true, false));
    fresh = stmd_create();
    assert(fresh);
    assertSame(preview, copyDocument(stmd_parse_streaming(fresh, payload.data(), payload.size(), true, false)));
    stmd_destroy(fresh);
    assert(ascii(stmd_parse_streaming(parser, payload.data(), payload.size(), true, false)).find("bold") != std::string::npos);
    const std::string complete = payload + "**";
    fresh = stmd_create();
    assertSame(copyDocument(stmd_parse_streaming(parser, complete.data(), complete.size(), true, false)),
        copyDocument(stmd_parse_streaming(fresh, complete.data(), complete.size(), true, false)));
    stmd_destroy(fresh);
    stmd_destroy(parser);
  }
}

static void assertInlineProjectionLiteralBlocks() {
  for (const std::string& suffix : {"    **bo", "    **bo ", "<div>\n**bo", "<div>\n**bo ",
       "    **", "\t**", "<div>\n**", "<div>[unfinished", "    [unfinished", "\t[unfinished",
       "<div>\n[unfinished", "<pre>\n\n[unfinished"}) {
    auto* parser = stmd_create();
    assert(parser);
    const auto streamed = copyDocument(stmd_parse_streaming(parser, suffix.data(), suffix.size(), true, false));
    auto* fresh = stmd_create();
    assert(fresh);
    assertSame(streamed, copyDocument(stmd_parse(fresh, suffix.data(), suffix.size(), true)));
    stmd_destroy(fresh);
    stmd_destroy(parser);

    // Exercise the bounded tail path beyond its 8 KiB seam at the chunk sizes
    // used by transport clients, then verify the terminal parse is literal.
    assertLiteralBlockProjectionChunks(std::string(8200, 'x') + "\n\n" + suffix);
  }
}

static void assertNoStreamingSyntaxFlash() {
  auto* parser = stmd_create();
  assert(parser);
  const auto progressive = [&](const std::string& input) {
    const auto* doc = stmd_parse_streaming(parser, input.data(), input.size(), true, false);
    assert(doc && doc->error == 0 && doc->sourceBytesConsumed == input.size());
    return ascii(doc);
  };
  const auto link = progressive("before [read](https://example.test");
  assert(link.find("before read") != std::string::npos && link.find("https://") == std::string::npos && link.find('[') == std::string::npos);
  const std::string pendingImageSource = "before ![cover](https://example.test/image.png";
  const auto* pendingImage = stmd_parse_streaming(parser, pendingImageSource.data(), pendingImageSource.size(), true, false);
  const auto image = ascii(pendingImage);
  assert(image.find("before ") != std::string::npos && image.find("https://") == std::string::npos && image.find('!') == std::string::npos);
  assert(pendingImage->blockCount == 1 && pendingImage->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING &&
      pendingImage->blocks[0].url == nullptr && copyUtf16(pendingImage->blocks[0].alt, pendingImage->blocks[0].altLength) == u"cover");
  assert(progressive("before **emphasis").find("before emphasis") != std::string::npos);
  for (const std::string& marker : {"Before *", "Before **", "Before `"}) {
    const auto rendered = progressive(marker);
    assert(rendered.find('*') == std::string::npos && rendered.find('`') == std::string::npos);
  }
  assert(progressive("**bold*").find("bold") != std::string::npos);
  const std::string imageLabelSource = "before ![alt]";
  const auto* imageLabelDoc = stmd_parse_streaming(parser, imageLabelSource.data(), imageLabelSource.size(), true, false);
  const auto imageLabel = ascii(imageLabelDoc);
  assert(imageLabel.find('!') == std::string::npos && imageLabel.find('[') == std::string::npos && imageLabel.find(']') == std::string::npos);
  assert(imageLabelDoc->blockCount == 1 && imageLabelDoc->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING);
  const auto linkLabel = progressive("before [alt]");
  assert(linkLabel.find('[') == std::string::npos && linkLabel.find(']') == std::string::npos);
  const auto nested = progressive("before **bold *italic");
  assert(nested.find("**") == std::string::npos && nested.find("italic") != std::string::npos);
  const auto mixed = progressive("before ~~**mixed");
  assert(mixed.find("~~") == std::string::npos && mixed.find("**") == std::string::npos);
  assert(has(stmd_parse_streaming(parser, "before **bold *italic", 21, true, false), STMD_RUN_BOLD));
  // A leading-pipe table header is visible as it is written; its generated
  // delimiter is private to the projection and never appears in text/cells.
  const std::string provisionalOne = "| Fea";
  const auto* oneCell = stmd_parse_streaming(parser, provisionalOne.data(), provisionalOne.size(), true, false);
  assert(oneCell->blockCount == 1 && oneCell->blocks[0].kind == STMD_BLOCK_TABLE &&
      oneCell->blocks[0].rowCount == 1 && oneCell->blocks[0].columnCount == 1 &&
      copyUtf16(oneCell->blocks[0].cells[0].text, oneCell->blocks[0].cells[0].textLength) == u"Fea");
  const std::string provisionalTwo = "| Feature | Sta";
  const auto* twoCells = stmd_parse_streaming(parser, provisionalTwo.data(), provisionalTwo.size(), true, false);
  assert(twoCells->blockCount == 1 && twoCells->blocks[0].rowCount == 1 && twoCells->blocks[0].columnCount == 2 &&
      copyUtf16(twoCells->blocks[0].cells[0].text, twoCells->blocks[0].cells[0].textLength) == u"Feature" &&
      copyUtf16(twoCells->blocks[0].cells[1].text, twoCells->blocks[0].cells[1].textLength) == u"Sta");
  const std::string partialDelimiter = "| Feature | Status |\n| :--";
  const auto* delimiter = stmd_parse_streaming(parser, partialDelimiter.data(), partialDelimiter.size(), true, false);
  assert(delimiter->blockCount == 1 && delimiter->blocks[0].kind == STMD_BLOCK_TABLE &&
      delimiter->blocks[0].rowCount == 1 && delimiter->blocks[0].columnCount == 2);
  // Non-delimiter prose abandons the preview and is parsed exactly as source.
  const std::string rejectedTable = "| Feature | Status |\nthis is prose";
  const auto rejected = copyDocument(stmd_parse_streaming(parser, rejectedTable.data(), rejectedTable.size(), true, false));
  auto* freshRejected = stmd_create();
  assertSame(rejected, copyDocument(stmd_parse(freshRejected, rejectedTable.data(), rejectedTable.size(), true)));
  stmd_destroy(freshRejected);
  // Inline pipes remain ordinary prose; only leading-pipe table candidates are
  // allowed to use the reversible table projection.
  const std::string inlinePipe = "A | B is ordinary prose";
  const auto* inlinePipeDoc = stmd_parse_streaming(parser, inlinePipe.data(), inlinePipe.size(), true, false);
  assert(inlinePipeDoc->blockCount == 0);
  assert(progressive("prose | Header | ").find("prose | Header |") != std::string::npos);
  const auto completeTable = progressive("prose\n\n| A | B |\n| --- | --- |");
  assert(completeTable.find('|') == std::string::npos);
  const std::string headerOnlyTable = "prose\n\n| A | B |\n| --- | --- |";
  const auto* headerOnly = stmd_parse_streaming(parser, headerOnlyTable.data(), headerOnlyTable.size(), true, false);
  assert(headerOnly->blockCount == 1 && headerOnly->blocks[0].kind == STMD_BLOCK_TABLE &&
      headerOnly->blocks[0].rowCount == 1 && headerOnly->blocks[0].columnCount == 2 &&
      copyUtf16(headerOnly->blocks[0].cells[0].text, headerOnly->blocks[0].cells[0].textLength) == u"A");
  const auto tableRow = progressive("prose\n\n| A | B |\n| --- | --- |\n| one | two |\n");
  assert(tableRow.find('|') == std::string::npos);
  const std::string partialRowTable = "| A | B |\n| --- | --- |\n| Fir";
  const auto* partialRow = stmd_parse_streaming(parser, partialRowTable.data(), partialRowTable.size(), true, false);
  assert(partialRow->blockCount == 1 && partialRow->blocks[0].rowCount == 2 &&
      copyUtf16(partialRow->blocks[0].cells[2].text, partialRow->blocks[0].cells[2].textLength) == u"Fir" &&
      partialRow->blocks[0].cells[3].textLength == 0);
  const std::string noLeadingPipeRow = "| A | B |\n| --- | --- |\nFir";
  const auto* noLeadingPipe = stmd_parse_streaming(parser, noLeadingPipeRow.data(), noLeadingPipeRow.size(), true, false);
  assert(noLeadingPipe->blockCount == 1 && noLeadingPipe->blocks[0].rowCount == 2 &&
      copyUtf16(noLeadingPipe->blocks[0].cells[2].text, noLeadingPipe->blocks[0].cells[2].textLength) == u"Fir");
  const std::string twoDataRows = "| A | B |\n| --- | --- |\n| one | two |\n| three | four";
  const auto* twoRows = stmd_parse_streaming(parser, twoDataRows.data(), twoDataRows.size(), true, false);
  assert(twoRows->blockCount == 1 && twoRows->blocks[0].rowCount == 3 && twoRows->blocks[0].columnCount == 2 &&
      copyUtf16(twoRows->blocks[0].cells[4].text, twoRows->blocks[0].cells[4].textLength) == u"three" &&
      copyUtf16(twoRows->blocks[0].cells[5].text, twoRows->blocks[0].cells[5].textLength) == u"four");
  for (const std::string& fence : {"```", "```typescript", "~~~", "~~~python"}) {
    const auto* codeFence = stmd_parse_streaming(parser, fence.data(), fence.size(), true, false);
    assert(codeFence->textLength > 0 && ascii(codeFence).find('`') == std::string::npos && ascii(codeFence).find('~') == std::string::npos);
    bool styledCode = false;
    for (size_t run = 0; run < codeFence->runCount; ++run) {
      styledCode = styledCode || ((codeFence->runs[run].flags & STMD_RUN_CODE_BLOCK) != 0 && codeFence->runs[run].length != 0);
    }
    assert(styledCode);
  }
  for (const std::string& literalTableContext : {"    | Name", "<pre>\n| Name",
       "| Old |\n| --- |\n\n<pre>\n| Name",
       "| Old |\n| --- |\n\n<!--\n| Name"}) {
    const auto streamedContext = copyDocument(stmd_parse_streaming(parser, literalTableContext.data(), literalTableContext.size(), true, false));
    auto* freshContext = stmd_create();
    assertSame(streamedContext, copyDocument(stmd_parse(freshContext, literalTableContext.data(), literalTableContext.size(), true)));
    stmd_destroy(freshContext);
  }
  // Ending a confirmed row must not manufacture an extra delimiter/data row.
  for (const std::string& suffix : {"", "\n", "\n|", "\n| next"}) {
    const std::string source = "| A | B |\n| --- | --- |\n| one | two |" + suffix;
    const auto actual = copyDocument(stmd_parse_streaming(parser, source.data(), source.size(), true, false));
    auto* freshRow = stmd_create();
    assertSame(actual, copyDocument(stmd_parse(freshRow, source.data(), source.size(), true)));
    stmd_destroy(freshRow);
  }
  for (const std::string& info : {"py", "pyt", "python"}) {
    const std::string earlier = "```js\nconst old = 1;\n```\n\n";
    const std::string source = earlier + "```" + info;
    const auto* open = stmd_parse_streaming(parser, source.data(), source.size(), true, false);
    assert(has(open, STMD_RUN_LANGUAGE_JAVASCRIPT));
    assert(!has(open, STMD_RUN_LANGUAGE_PYTHON));
    const std::string completeInfo = source + "\n";
    const auto* complete = stmd_parse_streaming(parser, completeInfo.data(), completeInfo.size(), true, false);
    assert(has(complete, STMD_RUN_LANGUAGE_JAVASCRIPT));
    if (info != "pyt") assert(has(complete, STMD_RUN_LANGUAGE_PYTHON));
  }
  const std::string code = "```text\n![literal](https://example.test/image.png";
  assert(progressive(code).find("![literal](https://example.test/image.png") != std::string::npos);
  const std::string literal = "before [read](https://example.test";
  const auto streamed = copyDocument(stmd_parse_streaming(parser, literal.data(), literal.size(), true, false));
  const auto final = copyDocument(stmd_parse_streaming(parser, literal.data(), literal.size(), true, true));
  assert(streamed.text.size() < final.text.size());
  auto* fresh = stmd_create();
  assertSame(final, copyDocument(stmd_parse(fresh, literal.data(), literal.size(), true)));
  const auto completeItalic = progressive("complete *italic* before whitespace ");
  assert(completeItalic.find("complete italic before whitespace") != std::string::npos);
  assert(progressive("complete _italic_ before whitespace ").find("complete italic before whitespace") != std::string::npos);
  assert(progressive("snake_case and $5 * 2") == "snake_case and $5 * 2\n");
  assert(progressive("Hello **   ") == "Hello **\n");
  // ascii() represents the normalized U+2022 list marker as '?'.
  assert(progressive("* hello") == "? hello\n");
  assert(progressive("* item with *italic* text") == "? item with italic text\n");
  assert(progressive("**bold** rest") == "bold rest\n");
  assert(progressive("*italic* text") == "italic text\n");
  assert(progressive("~~gone~~ rest") == "gone rest\n");
  assert(progressive("escaped \\*literal and `code * literal`").find("escaped *literal and code * literal") != std::string::npos);
  assert(progressive("``code ` literal``").find("code ` literal") != std::string::npos);
  const std::string commonStrike = "~~gone";
  const auto* commonStrikeDoc = stmd_parse_streaming(parser, commonStrike.data(), commonStrike.size(), false, false);
  assert(ascii(commonStrikeDoc) == "~~gone\n");
  const std::string inlineFence = "```inline code``` and *italic";
  const auto inlineFenceStreamed = copyDocument(stmd_parse_streaming(parser, inlineFence.data(), inlineFence.size(), true, false));
  assert(ascii(stmd_parse_streaming(parser, inlineFence.data(), inlineFence.size(), true, false)).find("inline code and italic") != std::string::npos);
  auto* freshInlineFence = stmd_create();
  const auto inlineFenceFinal = copyDocument(stmd_parse_streaming(parser, inlineFence.data(), inlineFence.size(), true, true));
  assertSame(inlineFenceFinal, copyDocument(stmd_parse(freshInlineFence, inlineFence.data(), inlineFence.size(), true)));
  stmd_destroy(freshInlineFence);
  assert(!inlineFenceStreamed.text.empty());
  const std::string malformedFence = "```js\n```ts\n![alt](https://example.test";
  const auto malformedStreamed = copyDocument(stmd_parse_streaming(parser, malformedFence.data(), malformedFence.size(), true, false));
  auto* freshMalformedFence = stmd_create();
  assertSame(malformedStreamed, copyDocument(stmd_parse(freshMalformedFence, malformedFence.data(), malformedFence.size(), true)));
  stmd_destroy(freshMalformedFence);
  const std::string listThenCode = "- item\n  ```js";
  const auto* listCodeDoc = stmd_parse_streaming(parser, listThenCode.data(), listThenCode.size(), true, false);
  size_t firstCode = listCodeDoc->textLength;
  for (size_t run = 0; run < listCodeDoc->runCount; ++run) {
    if ((listCodeDoc->runs[run].flags & STMD_RUN_CODE_BLOCK) != 0) firstCode = std::min(firstCode, listCodeDoc->runs[run].start);
  }
  assert(firstCode >= 7);
  for (size_t run = 0; run < listCodeDoc->runCount; ++run) {
    if (listCodeDoc->runs[run].start < firstCode) assert((listCodeDoc->runs[run].flags & STMD_RUN_CODE_BLOCK) == 0);
  }
  const std::string task = "- [x] Done";
  const auto* taskDoc = stmd_parse_streaming(parser, task.data(), task.size(), true, false);
  assert(taskDoc->runCount > 0 && taskDoc->runs[0].length == 4 &&
      (taskDoc->runs[0].flags & (STMD_RUN_TASK_MARKER | STMD_RUN_TASK_CHECKED)) ==
          (STMD_RUN_TASK_MARKER | STMD_RUN_TASK_CHECKED));
  assert(taskDoc->runCount > 1 && (taskDoc->runs[1].flags & STMD_RUN_TASK_MARKER) == 0);
  stmd_destroy(fresh);
  stmd_destroy(parser);
}

static void assertPendingImageContexts() {
  auto* parser = stmd_create();
  assert(parser);
  const auto pending = [&](const std::string& source) {
    const auto* doc = stmd_parse_streaming(parser, source.data(), source.size(), true, false);
    assert(doc && doc->error == 0);
    return doc;
  };
  const std::string nested = "before ![a [b] c";
  const auto* nestedDoc = pending(nested);
  assert(nestedDoc->blockCount == 1 && nestedDoc->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING &&
      copyUtf16(nestedDoc->blocks[0].alt, nestedDoc->blocks[0].altLength) == u"a [b] c");
  const std::string trailingText = "before ![label] ordinary text";
  const auto streamedTrailing = copyDocument(pending(trailingText));
  auto* freshTrailing = stmd_create();
  assertSame(streamedTrailing, copyDocument(stmd_parse(freshTrailing, trailingText.data(), trailingText.size(), true)));
  stmd_destroy(freshTrailing);
  assert(streamedTrailing.blocks.empty());
  const std::string referenceImage = "[ref]: https://example.test/image.png\n\n![label][ref] tail";
  const auto streamedReference = copyDocument(pending(referenceImage));
  auto* freshReference = stmd_create();
  assertSame(streamedReference, copyDocument(stmd_parse(freshReference, referenceImage.data(), referenceImage.size(), true)));
  stmd_destroy(freshReference);
  assert(streamedReference.blocks.size() == 1 && streamedReference.blocks[0].kind == STMD_BLOCK_IMAGE);
  const std::string referenceAtBoundary = "[ref]: https://example.test/image.png\n\n![label][ref]";
  const auto streamedBoundary = copyDocument(pending(referenceAtBoundary));
  auto* freshBoundary = stmd_create();
  assertSame(streamedBoundary, copyDocument(stmd_parse(freshBoundary, referenceAtBoundary.data(), referenceAtBoundary.size(), true)));
  stmd_destroy(freshBoundary);
  assert(streamedBoundary.blocks.size() == 1 && streamedBoundary.blocks[0].kind == STMD_BLOCK_IMAGE &&
      streamedBoundary.blocks[0].url == "https://example.test/image.png");
  const std::string escaped = "before \\![literal](https://example.test";
  const auto* escapedDoc = pending(escaped);
  assert(escapedDoc->blockCount == 0);
  const std::string inlineCode = "before `![literal](https://example.test";
  const auto* inlineCodeDoc = pending(inlineCode);
  assert(inlineCodeDoc->blockCount == 0);
  const std::string indentedCode = "    ![literal](https://example.test";
  const auto* indentedCodeDoc = pending(indentedCode);
  assert(indentedCodeDoc->blockCount == 0);
  const std::string inTable = "| A |\n| --- |\n| ![literal](https://example.test";
  const auto* tableDoc = pending(inTable);
  assert(tableDoc->blockCount == 1 && tableDoc->blocks[0].kind == STMD_BLOCK_TABLE &&
      tableDoc->blocks[0].rowCount == 2 && tableDoc->blocks[0].cells[1].textLength == 7 &&
      copyUtf16(tableDoc->blocks[0].cells[1].text, tableDoc->blocks[0].cells[1].textLength) == u"literal");
  const std::string imageHeader = "| ![header";
  const auto* headerDoc = pending(imageHeader);
  assert(headerDoc->blockCount == 1 && headerDoc->blocks[0].kind == STMD_BLOCK_TABLE &&
      headerDoc->blocks[0].rowCount == 1 && headerDoc->blocks[0].cells[0].textLength == 6 &&
      copyUtf16(headerDoc->blocks[0].cells[0].text, headerDoc->blocks[0].cells[0].textLength) == u"header");
  // A user-owned URL matching our internal marker cannot change an earlier
  // image when a later syntactically invalid opener is streamed.
  const std::string markerCollision = "![normal](streamtext:pending)\n\n    ![literal](https://example.test";
  const auto* collisionDoc = pending(markerCollision);
  assert(collisionDoc->blockCount == 1 && collisionDoc->blocks[0].kind == STMD_BLOCK_IMAGE &&
      collisionDoc->blocks[0].url && std::string(collisionDoc->blocks[0].url) == "streamtext:pending");
  stmd_destroy(parser);
}

static void assertNoRawLinkOrImagePrefixes(const std::string& source) {
  auto* parser = stmd_create();
  assert(parser);
  for (size_t length = 0; length <= source.size(); ++length) {
    const auto* doc = stmd_parse_streaming(parser, source.data(), length, true, false);
    assert(doc && doc->error == 0);
    const auto visible = ascii(doc);
    assert(visible.find('[') == std::string::npos && visible.find(']') == std::string::npos &&
        visible.find('!') == std::string::npos && visible.find("https://") == std::string::npos);
  }
  stmd_destroy(parser);
}

static void assertAnnouncementPrefixProjection() {
  const std::string emphasis =
      "Daylight should feel like a quiet desk: **one meaningful intention**, a gentle timer, and a closing ritual. "
      "*Avoid noisy streaks and leaderboards.* ~~More features~~ **more clarity** is the launch principle.";
  const std::string table =
      "Before the schedule:\n\n| When | Experience | Purpose |\n| :--- | :--- | :--- |\n"
      "| Morning | One focus card | Find clarity |\n| Midday | Gentle reset | Start again |\n\n"
      "A separate paragraph between tables.\n\n| Next | Owner |\n| :--- | ---: |\n| Review | Team |\n";
  auto* parser = stmd_create();
  assert(parser);
  for (const auto* source : {&emphasis, &table}) {
    for (size_t length = 0; length <= source->size(); ++length) {
      const auto* doc = stmd_parse_streaming(parser, source->data(), length, true, false);
      assert(doc && doc->error == 0);
      const auto visible = ascii(doc);
      if (source == &emphasis) {
        // This fixture contains no literal stars or tildes: each such byte
        // belongs to Markdown syntax and must never flash while streaming.
        assert(visible.find('*') == std::string::npos && visible.find('~') == std::string::npos);
      } else {
        // A lone opening pipe is held as ordinary source until its first cell
        // byte arrives; from that point the provisional table tests above
        // verify the grid path and this prefix loop verifies parser safety.
        assert(!visible.empty() || length == 0);
      }
    }
  }
  stmd_destroy(parser);
}

static uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5;
  return state;
}
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

static void assertOptionalChangeTracking() {
  auto* parser = stmd_create();
  assert(parser);
  const std::string first = "before **first**";
  const std::string second = "before **second**";
  stmd_parse(parser, first.data(), first.size(), true);
  stmd_set_track_changes(parser, false);
  const auto disabled = copyDocument(stmd_parse(parser, second.data(), second.size(), true));
  const auto* disabledDoc = stmd_parse(parser, second.data(), second.size(), true);
  assert(disabledDoc->changedStartUTF16 == 0);
  auto* fresh = stmd_create();
  assertSame(disabled, copyDocument(stmd_parse(fresh, second.data(), second.size(), true)));
  stmd_destroy(fresh);
  // Re-enabling begins a new history; it must never reuse a stale snapshot.
  stmd_set_track_changes(parser, true);
  const auto* reenabled = stmd_parse(parser, first.data(), first.size(), true);
  assert(reenabled->changedStartUTF16 == 0);
  const auto* changed = stmd_parse(parser, second.data(), second.size(), true);
  assert(changed->changedStartUTF16 <= changed->textLength);
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

#ifdef STMD_TESTING
extern "C" size_t stmd_test_parse_calls(const STMarkdownParser* parser);
static void assertRejectedProjectionWork() {
  const std::vector<std::string> contexts = {
    "    ![alt", "\t![alt", "<!--\n| Name", "<pre>\n| Name",
    "<script>\n![alt", "<div>\n![alt", "<!--\n![alt", "```\n![alt",
    "| A |\n| --- |\n| first |\n| ![alt",
  };
  for (const auto& initial : contexts) {
    auto* parser = stmd_create();
    auto* reference = stmd_create();
    std::string source = initial;
    for (size_t step = 0; step < 40; ++step) {
      const size_t before = stmd_test_parse_calls(parser);
      const auto actual = copyDocument(stmd_parse_streaming(parser, source.data(), source.size(), true, false));
      const size_t calls = stmd_test_parse_calls(parser) - before;
      const bool tableImage = initial.rfind("| A |", 0) == 0;
      if (calls != (tableImage ? 1u : (step == 0 ? 2u : 1u))) {
        std::cerr << "Rejected projection calls=" << calls << " step=" << step << " source=" << source << '\n';
        std::abort();
      }
      if (tableImage) {
        assert(actual.blocks.size() == 1 && actual.blocks[0].kind == STMD_BLOCK_TABLE &&
            actual.blocks[0].cells.size() == 3 && actual.blocks[0].cells.back().text ==
                std::u16string(u"alt") + std::u16string(step, u'a'));
      } else {
        assertSame(actual, copyDocument(stmd_parse(reference, source.data(), source.size(), true)));
      }
      source += "a";
    }
    // A terminal status and subsequent reuse must not keep the negative cache.
    const auto terminal = copyDocument(stmd_parse_streaming(parser, source.data(), source.size(), true, true));
    assertSame(terminal, copyDocument(stmd_parse(reference, source.data(), source.size(), true)));
    const size_t before = stmd_test_parse_calls(parser);
    stmd_parse_streaming(parser, source.data(), source.size(), true, false);
    assert(stmd_test_parse_calls(parser) - before == (initial.rfind("| A |", 0) == 0 ? 1u : 2u));
    stmd_destroy(reference); stmd_destroy(parser);
  }

  auto* parser = stmd_create();
  auto* reference = stmd_create();
  // Close a literal HTML block on the same line, then start a real image/table
  // on the next one. Cache invalidation must honor CR as well as LF.
  for (const auto& newline : {std::string("\n"), std::string("\r\n"), std::string("\r")}) {
    std::string source = "<pre>\n![alt";
    stmd_parse_streaming(parser, source.data(), source.size(), true, false);
    source += "]</pre>";
    auto before = stmd_test_parse_calls(parser);
    assertSame(copyDocument(stmd_parse_streaming(parser, source.data(), source.size(), true, false)),
               copyDocument(stmd_parse(reference, source.data(), source.size(), true)));
    assert(stmd_test_parse_calls(parser) - before == 1);
    source += newline + newline + "![real";
    auto* document = stmd_parse_streaming(parser, source.data(), source.size(), true, false);
    assert(document->blockCount == 1 && document->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING);
  }

  // A changed prefix outside the bounded tail must invalidate the cache even
  // when total length, allocation address, candidate position and tail match.
  std::string source = "<pre>\n" + std::string(9000, 'x') + "\n\n| Name";
  stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  source.replace(0, 5, "plain");
  auto* document = stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  assert(document->blockCount == 1 && document->blocks[0].kind == STMD_BLOCK_TABLE);

  // The literal-fence cache is exact: replacing bytes outside the tail turns
  // the same unfinished image into a real pending image immediately.
  source = "```\n![alt";
  stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  source.replace(0, 3, "text");
  document = stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  assert(document->blockCount == 1 && document->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING);

  // A four-space continuation does not interrupt a paragraph: it can contain
  // a real inline image. Never suppress it by guessing from indentation alone.
  source = "paragraph\n    ![real";
  document = stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  assert(document->blockCount == 1 && document->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING);

  // Dialect and BOM handling are part of the cache key, not just source bytes.
  source = "| A |\n| --- |\n| row |\n| ![alt";
  stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  document = stmd_parse_streaming(parser, source.data(), source.size(), false, false);
  assert(document->blockCount == 1 && document->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING);
  source = "\xef\xbb\xbf    ![alt";
  stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  document = stmd_parse_decoded_streaming(parser, source.data(), source.size(), true, false);
  assert(document->blockCount == 1 && document->blocks[0].kind == STMD_BLOCK_IMAGE_PENDING);

  // Once a table image closes, ordinary inline projection must remain live.
  source = "| A |\n| --- |\n| row |\n| ![alt";
  stmd_parse_streaming(parser, source.data(), source.size(), true, false);
  source += "](https://example.test/a) **bold";
  auto* fresh = stmd_create();
  assertSame(copyDocument(stmd_parse_streaming(parser, source.data(), source.size(), true, false)),
             copyDocument(stmd_parse_streaming(fresh, source.data(), source.size(), true, false)));
  stmd_destroy(fresh);
  stmd_destroy(reference); stmd_destroy(parser);
}
#endif

int main(int argc, char** argv) {
#ifdef STMD_TESTING
  assertRejectedProjectionWork();
#endif
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
  const std::string image = "before ![caf\xc3\xa9](https://example.test/image.png) after";
  doc = stmd_parse(parser, image.data(), image.size(), true);
  assert(doc->error == 0 && doc->blockCount == 1 && doc->blocks[0].kind == STMD_BLOCK_IMAGE);
  assert(doc->blocks[0].length == 1 && doc->blocks[0].url && std::string(doc->blocks[0].url) == "https://example.test/image.png");
  assert(doc->blocks[0].altLength == 4 && doc->blocks[0].alt[0] == 'c');
  const std::string encodedImage = "![a &amp; &#x1F680;](https://example.test/a.png)";
  doc = stmd_parse(parser, encodedImage.data(), encodedImage.size(), true);
  assert(doc->blockCount == 1 && doc->blocks[0].altLength == 6 && doc->blocks[0].alt[2] == '&');
  const std::string literalAlt = "![`&amp;` \\&amp; &amp;](https://example.test/a.png)";
  doc = stmd_parse(parser, literalAlt.data(), literalAlt.size(), true);
  assert(std::u16string(doc->blocks[0].alt, doc->blocks[0].alt + doc->blocks[0].altLength) == u"&amp; &amp; &");
  const std::string nestedImage = "![outer ![inner](https://example.test/inner.png) end](https://example.test/outer.png)";
  doc = stmd_parse(parser, nestedImage.data(), nestedImage.size(), true);
  assert(doc->blockCount == 1);
  assert(std::u16string(doc->blocks[0].alt, doc->blocks[0].alt + doc->blocks[0].altLength) == u"outer inner end");
  const std::string cellImage = "| Image |\n| --- |\n| ![safe &amp; alt](https://example.test/cell.png) |\n";
  doc = stmd_parse(parser, cellImage.data(), cellImage.size(), true);
  assert(doc->blockCount == 1 && doc->blocks[0].kind == STMD_BLOCK_TABLE);
  const auto& imageCell = doc->blocks[0].cells[1];
  assert(std::u16string(imageCell.text, imageCell.text + imageCell.textLength) == u"safe & alt");
  const std::string table = "| left | center |\n|:-----|:------:|\n| **one** | [two](https://example.test) |\n";
  doc = stmd_parse(parser, table.data(), table.size(), true);
  assert(doc->error == 0 && doc->blockCount == 1 && doc->blocks[0].kind == STMD_BLOCK_TABLE);
  const auto& tableBlock = doc->blocks[0];
  assert(tableBlock.length == 1 && tableBlock.rowCount == 2 && tableBlock.columnCount == 2);
  assert(tableBlock.cells && tableBlock.cells[0].flags == STMD_BLOCK_CELL_HEADER && tableBlock.cells[1].alignment == STMD_ALIGN_CENTER);
  assert(tableBlock.cells[2].runs[0].flags & STMD_RUN_BOLD && tableBlock.cells[3].runs[0].flags & STMD_RUN_LINK);
  const std::string code = "```js\nconst value = \"text\"; // note\n```\n";
  doc = stmd_parse(parser, code.data(), code.size(), true);
  bool keyword = false, stringToken = false, comment = false;
  for (size_t i = 0; i < doc->runCount; ++i) {
    keyword |= (doc->runs[i].flags & STMD_RUN_TOKEN_KEYWORD) != 0;
    stringToken |= (doc->runs[i].flags & STMD_RUN_TOKEN_STRING) != 0;
    comment |= (doc->runs[i].flags & STMD_RUN_TOKEN_COMMENT) != 0;
    if (i) assert(doc->runs[i - 1].start + doc->runs[i - 1].length <= doc->runs[i].start);
  }
  assert(keyword && stringToken && comment);
  assert(has(doc, STMD_RUN_LANGUAGE_JAVASCRIPT));
  // Adjacent code blocks can coalesce a plain CODE run; both must still get tokenized.
  const std::string adjacentCode = "```unknown\nplain\n```\n```js\nconst next = 1;\n```\n";
  doc = stmd_parse(parser, adjacentCode.data(), adjacentCode.size(), true);
  assert(has(doc, STMD_RUN_TOKEN_KEYWORD) && has(doc, STMD_RUN_TOKEN_NUMBER));
  const std::string separatedCode = "```js\nconst first = 1;\n```\n\n```js\nconst second = 2;\n```\n";
  doc = stmd_parse(parser, separatedCode.data(), separatedCode.size(), true);
  bool plainCodeSeparator = false;
  for (size_t i = 0; i < doc->runCount; ++i) {
    const auto& run = doc->runs[i];
    if ((run.flags & STMD_RUN_CODE_BLOCK) == 0 && run.length != 0) {
      for (size_t at = run.start; at < run.start + run.length; ++at) {
        if (doc->text[at] == '\n') plainCodeSeparator = true;
      }
    }
  }
  assert(plainCodeSeparator);
  const std::string languageBefore = "```unknown\nplain\n```\n```unknown\nconst next = 1;\n```\n";
  stmd_parse(parser, languageBefore.data(), languageBefore.size(), true);
  doc = stmd_parse(parser, adjacentCode.data(), adjacentCode.size(), true);
  const std::u16string languageText(doc->text, doc->text + doc->textLength);
  assert(doc->changedStartUTF16 == languageText.find(u"const next")); // only the second code body changes style
  doc = stmd_parse(parser, adjacentCode.data(), adjacentCode.size(), true);
  assert(doc->changedStartUTF16 == doc->textLength);
  // Identical U+FFFC text must still invalidate precisely the changed attachment.
  const std::string stablePrefix = "![first](https://example.test/a.png)\n\ntext\n\n";
  auto metadataChange = [&](const std::string& before, const std::string& after) {
    const std::string initial = stablePrefix + before;
    const std::string final = stablePrefix + after;
    stmd_parse(parser, initial.data(), initial.size(), true);
    const auto* changed = stmd_parse(parser, final.data(), final.size(), true);
    assert(changed->blockCount == 2 && changed->changedStartUTF16 == changed->blocks[1].start);
    const auto* same = stmd_parse(parser, final.data(), final.size(), true);
    assert(same->changedStartUTF16 == same->textLength);
  };
  metadataChange("![old](https://example.test/a)", "![new](https://example.test/a)");
  metadataChange("![same](https://example.test/a)", "![same](https://example.test/b)");
  metadataChange("| A |\n| --- |\n| old |", "| A |\n| --- |\n| new |");
  metadataChange("| A |\n| --- |\n| same |", "| A |\n| --- |\n| **same** |");
  metadataChange("| A |\n| :--- |\n| same |", "| A |\n| ---: |\n| same |");
  metadataChange("| A |\n| --- |\n| [x](https://example.test/a) |", "| A |\n| --- |\n| [x](https://example.test/b) |");
  for (const auto& rich : {literalAlt, nestedImage, cellImage, table, code, adjacentCode}) assertStreamSeam(rich, true);
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
  const std::vector<std::pair<std::string, bool>> incompleteStreamingCases = {
      {"A [link](https://example.test/path) then ![image](https://example.test/a.png", true},
      {"before **unfinished strong", true},
      {"before _unfinished italic", true},
      {"A | B\n| --", true},
      {"```js\nconst literal = \"![not image](x\";\n", true},
      {"escaped \\[literal](x) and `![literal](x`", true},
      {"![escaped \\[label\\]](https://example.test/a_(b)", true},
      {"before ![label] ordinary text", true},
      {"[ref]: https://example.test/image.png\n\n![label][ref] tail", true},
      {"[ref]: https://example.test/image.png\n\n![label][ref]", true},
      {"prose\n| Header |\n| ---", true},
  };
  for (const auto& streaming : incompleteStreamingCases) assertStreamingProjectionSeam(streaming.first, streaming.second);
  assertInlineProjectionLiteralBlocks();
  assertMixedStreamingProjectionChunks();
  assertClosedHtmlDoesNotRejectFollowingProjection();
  assertNoStreamingSyntaxFlash();
  assertPendingImageContexts();
  assertNoRawLinkOrImagePrefixes("before [read](https://example.test/a_(b)) after");
  assertNoRawLinkOrImagePrefixes("before ![alt](https://example.test/a_(b)) after");
  assertAnnouncementPrefixProjection();
  // The tail scanner must not backtrack quadratically over escapes or attempt
  // a projection beyond its bounded single-line window.
  std::string longTail(1024 * 1024, '\\');
  longTail += "[not-a-link";
  doc = stmd_parse_streaming(parser, longTail.data(), longTail.size(), true, false);
  assert(doc && doc->error == 0 && doc->sourceBytesConsumed == longTail.size());
  // A short candidate line after a huge predecessor must not scan backward
  // beyond the streaming tail window while checking for a table header.
  std::string longPreviousLine(1024 * 1024, 'x');
  longPreviousLine += "\n| --";
  doc = stmd_parse_streaming(parser, longPreviousLine.data(), longPreviousLine.size(), true, false);
  assert(doc && doc->error == 0 && doc->sourceBytesConsumed == longPreviousLine.size());

  std::string deep;
  for (int depth = 0; depth < 96; ++depth) deep.append(static_cast<size_t>(depth) * 2, ' ').append("- nested\n");
  doc = stmd_parse(parser, deep.data(), deep.size(), true);
  assert(doc->error == 0 && doc->textLength > 0);
  assertAbiErrorAndRecovery();
  assertBorrowedDocumentSnapshot();
  assertOptionalChangeTracking();
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
      const auto streamingStart = std::chrono::steady_clock::now();
      for (int i = 0; i < iterations; ++i) {
        assert(stmd_parse_streaming(parser, input.data(), input.size(), true, false)->error == 0);
      }
      const auto streamingSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - streamingStart).count();
      std::string unfinished = input + "**incomplete";
      const auto projectedStart = std::chrono::steady_clock::now();
      for (int i = 0; i < iterations; ++i) {
        assert(stmd_parse_streaming(parser, unfinished.data(), unfinished.size(), true, false)->error == 0);
      }
      const auto projectedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - projectedStart).count();
      std::cout << "streaming final-shaped " << bytes << " bytes: " << (bytes * iterations / streamingSeconds / 1e6)
                << " MB/s; projected incomplete tail: " << (unfinished.size() * iterations / projectedSeconds / 1e6) << " MB/s\n";
      if (bytes == 100 * 1024) {
        std::string heldTail = input + "\n\n**stream remains open";
        const auto heldStart = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) {
          assert(stmd_parse_streaming(parser, heldTail.data(), heldTail.size(), true, false)->error == 0);
        }
        const auto heldSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - heldStart).count();
        std::cout << "streaming held-tail " << heldTail.size() << " bytes: "
                  << (heldTail.size() * iterations / heldSeconds / 1e6)
                  << " MB/s (full-prefix parse plus active-tail projection path)\n";
      }
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
