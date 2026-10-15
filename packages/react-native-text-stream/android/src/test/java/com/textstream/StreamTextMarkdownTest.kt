package com.textstream

import org.junit.Assert.assertEquals
import org.junit.Test

class StreamTextMarkdownTest {
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
