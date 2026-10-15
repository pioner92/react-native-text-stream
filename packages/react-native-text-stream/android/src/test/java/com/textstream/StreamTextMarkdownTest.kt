package com.textstream

import org.junit.Assert.assertEquals
import org.junit.Test

class StreamTextMarkdownTest {
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
