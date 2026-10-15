package com.textstream

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StreamTextMarkdownTest {

  @Test fun syntaxPaletteKeepsFourPointFiveContrastOnLightAndDarkSurfaces() {
    fun channel(color: Int, shift: Int): Double {
      val linear = ((color ushr shift) and 0xff) / 255.0
      return if (linear <= .04045) linear / 12.92 else Math.pow((linear + .055) / 1.055, 2.4)
    }
    fun luminance(color: Int) = .2126 * channel(color, 16) + .7152 * channel(color, 8) + .0722 * channel(color, 0)
    fun contrast(a: Int, b: Int): Double {
      val (bright, dark) = listOf(luminance(a), luminance(b)).sortedDescending()
      return (bright + .05) / (dark + .05)
    }
    fun over(foreground: Int, background: Int): Int {
      val alpha = (foreground ushr 24 and 0xff) / 255.0
      fun channel(shift: Int): Int {
        val front = foreground ushr shift and 0xff
        val back = background ushr shift and 0xff
        return (front * alpha + back * (1.0 - alpha)).toInt()
      }
      return 0xff000000.toInt() or (channel(16) shl 16) or (channel(8) shl 8) or channel(0)
    }
    val kinds = intArrayOf(StreamTextMarkdown.TOKEN_KEYWORD, StreamTextMarkdown.TOKEN_STRING,
        StreamTextMarkdown.TOKEN_COMMENT, StreamTextMarkdown.TOKEN_NUMBER, StreamTextMarkdown.TOKEN_TYPE)
    // MarkdownCodeBlockSpan draws the current text RGB with alpha 0x24 over the page. Verify
    // contrast against this real composited surface, not only the page behind it.
    val lightSurface = over(0x24101217, 0xffffffff.toInt())
    val darkSurface = over(0x24f2f4f8, 0xff0b1020.toInt())
    for (kind in kinds) {
      assertTrue("light $kind", contrast(StreamTextMarkdown.syntaxColor(0xff101217.toInt(), kind), lightSurface) >= 4.5)
      assertTrue("dark $kind", contrast(StreamTextMarkdown.syntaxColor(0xfff2f4f8.toInt(), kind), darkSurface) >= 4.5)
    }
  }

  @Test fun codeTokensUseOneParagraphMargin() {
    val margins = StreamTextMarkdown.codeBlockMarginRanges(
        "const value = 42\nnext", intArrayOf(0, 6, 14, 17), intArrayOf(5, 5, 2, 4),
        intArrayOf(StreamTextMarkdown.CODE_BLOCK or StreamTextMarkdown.TOKEN_KEYWORD,
            StreamTextMarkdown.CODE_BLOCK, StreamTextMarkdown.CODE_BLOCK or StreamTextMarkdown.TOKEN_NUMBER,
            StreamTextMarkdown.CODE_BLOCK))
    assertEquals(listOf(0 to 16, 17 to 21), margins)
  }

  @Test fun codeBlockMarginRangesRemainLinearForManyTokensAndMultilineRuns() {
    val text = "a b c d\ne f g\nnext"
    val margins = StreamTextMarkdown.codeBlockMarginRanges(
        text, intArrayOf(0, 2, 4, 6, 8, 14), intArrayOf(13, 1, 1, 1, 1, 4),
        IntArray(6) { StreamTextMarkdown.CODE_BLOCK })
    assertEquals(listOf(0 to 7, 8 to 13, 14 to 18), margins)
  }

  @Test fun codeBlockClusterRewindsSuffixToFirstFenceParagraph() {
    val text = "before\nconst answer = streamFetch(\n  '/plan'\n)\nafter"
    val starts = intArrayOf(7, 35, 45)
    // Native code runs include each source-body newline; the fence separator itself is plain.
    val lengths = intArrayOf(28, 10, 2)
    val flags = intArrayOf(StreamTextMarkdown.CODE_BLOCK, StreamTextMarkdown.CODE_BLOCK, StreamTextMarkdown.CODE_BLOCK)
    // A parser run can span more than one physical code paragraph; a later suffix must still
    // replace the first one so its single surface/header cannot overlap the new suffix spans.
    assertEquals(7, StreamTextMarkdown.codeBlockClusterStart(text, starts, lengths, flags, 35))
    assertEquals(7, StreamTextMarkdown.codeBlockClusterStart(text, starts, lengths, flags, 45))
    // A plain separator between adjacent fences is a real gap, so they must not become one
    // retained suffix section merely because their paragraphs sit next to each other.
    val separateFences = "alpha\n\nbeta\n"
    val fenceStarts = intArrayOf(0, 7)
    val fenceLengths = intArrayOf(6, 5)
    val fenceFlags = intArrayOf(StreamTextMarkdown.CODE_BLOCK, StreamTextMarkdown.CODE_BLOCK)
    assertEquals(0, StreamTextMarkdown.codeBlockClusterStart(separateFences, fenceStarts, fenceLengths, fenceFlags, 6))
    assertEquals(7, StreamTextMarkdown.codeBlockClusterStart(separateFences, fenceStarts, fenceLengths, fenceFlags, 7))
  }

  @Test fun codeLanguageHeaderAppearsOnceForAContiguousFence() {
    assertEquals(listOf("JS", null, "TS"), StreamTextMarkdown.codeLanguageSectionLabels(
        listOf(0 to 13, 14 to 27, 30 to 40), listOf("JS", "JS", "TS")))
  }

  @Test fun codeSurfaceKeepsSparseLanguageMetadataInOneContiguousSection() {
    val sections = StreamTextMarkdown.codeBlockSections(
        listOf(0 to 9, 10 to 19, 20 to 29, 31 to 40), listOf("TS", null, "TS", "JSON"))
    assertEquals(listOf<StreamTextMarkdown.Companion.CodeBlockSection>(
        StreamTextMarkdown.Companion.CodeBlockSection(0, 29, "TS", 9),
        StreamTextMarkdown.Companion.CodeBlockSection(31, 40, "JSON", 40),
    ), sections)
  }

  @Test fun multilineCodeRunKeepsMarginsForEveryCoveredParagraph() {
    val margins = StreamTextMarkdown.codeBlockMarginRanges(
        "one\ntwo\nplain", intArrayOf(0), intArrayOf(7), intArrayOf(StreamTextMarkdown.CODE_BLOCK))
    assertEquals(listOf(0 to 3, 4 to 7), margins)
  }

  @Test fun quoteAndIndentDecorationsAreOnePerTokenizedParagraph() {
    val decorations = StreamTextMarkdown.paragraphDecorations(
        "quoted tokens\nlist item", intArrayOf(0, 7, 14, 19), intArrayOf(6, 6, 4, 4),
        intArrayOf(StreamTextMarkdown.QUOTE, StreamTextMarkdown.QUOTE, 0, 0), intArrayOf(0, 0, 2, 2))
    assertEquals(listOf("0:13:true:0", "14:23:false:2"),
        decorations.map { "${it.start}:${it.end}:${it.quote}:${it.indent}" })
  }

  @Test fun taskMarkersRecognizeOnlyLineContentEdges() {
    assertEquals(listOf("0:true", "9:false", "20:true"),
        StreamTextMarkdown.taskMarkers("[x] done\n[ ] todo\n> [X] quoted\ninline [x] stays text",
            intArrayOf(0, 9, 18, 27), intArrayOf(8, 8, 12, 21), IntArray(4), intArrayOf(1, 1, 1, 0))
            .map { "${it.start}:${it.checked}" })
  }

  @Test fun taskMarkersHandleLargeListsWithoutScanningEveryRunPerLine() {
    val text = buildString { repeat(1000) { append("[x] row-").append(it).append('\n') } }
    val starts = IntArray(1000)
    var offset = 0
    repeat(1000) { index -> starts[index] = offset; offset += "[x] row-$index\n".length }
    val markers = StreamTextMarkdown.taskMarkers(text, starts, IntArray(1000) { 8 }, IntArray(1000), IntArray(1000) { 1 })
    assertEquals(1000, markers.size)
    assertEquals(0, markers.first().start)
    assertEquals(starts.last(), markers.last().start)
  }
  private fun cell(text: String = "cell", flags: Int = 0, url: String? = null, alignment: Int = 1, header: Boolean = false) =
      StreamTextMarkdown.RichCell(text, intArrayOf(0), intArrayOf(text.length), intArrayOf(flags),
          intArrayOf(0), intArrayOf(0), arrayOf(url), header, alignment)
  private fun rich(block: StreamTextMarkdown.RichBlock) = StreamTextMarkdown("prefix\n\uFFFC\n",
      intArrayOf(0), intArrayOf(9), intArrayOf(0), intArrayOf(0), intArrayOf(0), arrayOf(null), arrayOf(block))
  private fun table(cell: StreamTextMarkdown.RichCell) = StreamTextMarkdown.RichBlock(1, 7, 1, 1, arrayOf(cell), null, null)

  @Test fun accessibleTailUsesAbsoluteAttachmentOffsetsAndBoundedExpansion() {
    val text = "\uFFFC\n" + "x".repeat(2500) + "\n\uFFFC\nafter"
    val blocks = arrayOf(
      StreamTextMarkdown.RichBlock(2, 0, 0, 0, null, "https://a", "old image"),
      StreamTextMarkdown.RichBlock(2, 2503, 0, 0, null, "https://b", "new image"))
    val document = StreamTextMarkdown(text, intArrayOf(), intArrayOf(), intArrayOf(), intArrayOf(), intArrayOf(), emptyArray(), blocks)
    assertEquals("…\nimage new image\nafter", document.accessibleReading(2502, 2000).text)
    val longTable = rich(table(cell("🚀".repeat(5000)))).accessibleReading(0, 100).text
    org.junit.Assert.assertTrue(longTable.length <= 100)
    org.junit.Assert.assertFalse(Character.isHighSurrogate(longTable.last()))
  }

  @Test fun accessibleReadingDescribesParserConfirmedTaskState() {
    val text = "[x] done\n[ ] todo\nprose [x] stays"
    val document = document(text, intArrayOf(0, 9, 18), intArrayOf(8, 8, 17), IntArray(3),
        IntArray(3), intArrayOf(1, 1, 0), arrayOf(null, null, null))
    assertEquals("Checked task done\nUnchecked task todo\nprose [x] stays", document.accessibleReading(0, 200).text)
  }

  @Test fun accessibleReadingKeepsSemanticExpansionInsideTheVisibleSourceRange() {
    val text = "[x] done\n[ ] next\noffscreen"
    val document = document(text, intArrayOf(0, 9, 18), intArrayOf(8, 8, 9), IntArray(3),
        IntArray(3), intArrayOf(1, 1, 0), arrayOf(null, null, null))
    val visible = document.accessibleReading(0, 200, sourceEnd = 17).text
    assertEquals("Checked task done\nUnchecked task next", visible)
    assertTrue("visible range must not bleed into later source", !visible.contains("offscreen"))
  }

  @Test fun tableMetadataChangesInvalidateTheUnchangedPlaceholder() {
    val original = rich(table(cell()))
    for (changed in listOf(cell(text = "new"), cell(flags = StreamTextMarkdown.BOLD),
        cell(url = "https://example.test"), cell(alignment = 3), cell(header = true))) {
      assertEquals(7, rich(table(changed)).changedStartFrom(original))
    }
    assertEquals(9, rich(table(cell())).changedStartFrom(original))
  }

  @Test fun imageUrlAndAltChangesInvalidateTheUnchangedPlaceholder() {
    fun image(url: String, alt: String) = rich(StreamTextMarkdown.RichBlock(2, 7, 0, 0, null, url, alt))
    val original = image("https://example.test/one", "old")
    assertEquals(7, image("https://example.test/two", "old").changedStartFrom(original))
    assertEquals(7, image("https://example.test/one", "new").changedStartFrom(original))
    assertEquals(9, image("https://example.test/one", "old").changedStartFrom(original))
  }
  private fun document(
      text: String,
      starts: IntArray = IntArray(0),
      lengths: IntArray = IntArray(0),
      flags: IntArray = IntArray(0),
      headings: IntArray = IntArray(0),
      indents: IntArray = IntArray(0),
      urls: Array<String?> = emptyArray(),
  ) = StreamTextMarkdown(text, starts, lengths, flags, headings, indents, urls)

  @Test fun semanticOnlyChangeStartsAtTheChangedRun() {
    val plain = document("nested emphasis")
    val nested = document(
        "nested emphasis",
        intArrayOf(0, 7), intArrayOf(14, 8),
        intArrayOf(StreamTextMarkdown.BOLD, StreamTextMarkdown.ITALIC),
        intArrayOf(0, 0), intArrayOf(0, 0), arrayOf(null, null))

    assertEquals(0, nested.changedStartFrom(plain))
  }

  @Test fun laterSemanticChangeKeepsTheUntouchedPrefix() {
    val before = document(
        "title and body",
        intArrayOf(0, 10), intArrayOf(5, 4),
        intArrayOf(StreamTextMarkdown.BOLD, StreamTextMarkdown.ITALIC),
        intArrayOf(0, 0), intArrayOf(0, 0), arrayOf(null, null))
    val after = document(
        "title and body",
        intArrayOf(0, 10), intArrayOf(5, 4),
        intArrayOf(StreamTextMarkdown.BOLD, StreamTextMarkdown.STRIKE),
        intArrayOf(0, 0), intArrayOf(0, 0), arrayOf(null, null))

    assertEquals(10, after.changedStartFrom(before))
  }

  @Test fun urlChangesAreSemanticChangesEvenWhenVisibleTextMatches() {
    val first = document(
        "read docs", intArrayOf(5), intArrayOf(4), intArrayOf(StreamTextMarkdown.LINK),
        intArrayOf(0), intArrayOf(0), arrayOf("https://one.example/"))
    val second = document(
        "read docs", intArrayOf(5), intArrayOf(4), intArrayOf(StreamTextMarkdown.LINK),
        intArrayOf(0), intArrayOf(0), arrayOf("mailto:docs@example.test"))

    assertEquals(5, second.changedStartFrom(first))
  }

  @Test fun unicodeTextDiffUsesUtf16Offsets() {
    val previous = document("hello 😀 one")
    val next = document("hello 😀 two")

    // Kotlin's String indexing is UTF-16; the replacement point is after the emoji surrogate pair.
    assertEquals("hello 😀 ".length, next.changedStartFrom(previous))
  }

  @Test fun addedTrailingRunDoesNotForceReflowOfTheWholeDocument() {
    val previous = document("prefix code")
    val next = document(
        "prefix code", intArrayOf(7), intArrayOf(4), intArrayOf(StreamTextMarkdown.CODE),
        intArrayOf(0), intArrayOf(0), arrayOf(null))

    assertEquals(7, next.changedStartFrom(previous))
  }
}
