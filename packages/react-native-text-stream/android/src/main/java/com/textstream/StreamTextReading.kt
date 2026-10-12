package com.textstream

import android.view.accessibility.AccessibilityNodeInfo
import java.text.BreakIterator
import java.util.Locale
import kotlin.math.max
import kotlin.math.min

/**
 * What accessibility reads of a view: whole lines of its text, and where each starts in it. TalkBack
 * moves through it by character, word, line or paragraph; a View only iterates its content
 * description, so these are AOSP's iterators (android.view.AccessibilityIterators) over this text.
 */
internal class StreamTextReading(val text: String, lineStarts: IntArray) {
  /** The first is 0: text before the first line start (a "…") reads with that line. */
  private val starts = if (lineStarts.isEmpty()) intArrayOf(0) else lineStarts.copyOf().also { it[0] = 0 }

  /** The segment from `offset` on, as [start, end); null past the last. */
  fun following(granularity: Int, offset: Int): IntArray? {
    if (text.isEmpty() || offset >= text.length) return null
    val from = max(offset, 0)
    return when (granularity) {
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER -> breakFollowing(characters, from)
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD -> wordFollowing(from)
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE -> lineFollowing(from)
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_PARAGRAPH -> paragraphFollowing(from)
      else -> null
    }
  }

  /** The segment up to `offset`, as [start, end); null before the first. */
  fun preceding(granularity: Int, offset: Int): IntArray? {
    if (text.isEmpty() || offset <= 0) return null
    val to = min(offset, text.length)
    return when (granularity) {
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER -> breakPreceding(characters, to)
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD -> wordPreceding(to)
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE -> linePreceding(to)
      AccessibilityNodeInfo.MOVEMENT_GRANULARITY_PARAGRAPH -> paragraphPreceding(to)
      else -> null
    }
  }

  // MARK: - Lines

  private fun lineStart(line: Int) = starts[line]

  private fun lineEnd(line: Int) = if (line + 1 < starts.size) starts[line + 1] else text.length

  private fun lineFor(offset: Int): Int {
    var low = 0
    var high = starts.size - 1
    while (low < high) {
      val mid = (low + high + 1) ushr 1
      if (starts[mid] <= offset) low = mid else high = mid - 1
    }
    return low
  }

  private fun lineFollowing(from: Int): IntArray? {
    val current = lineFor(from)
    val next = if (lineStart(current) == from) current else current + 1
    if (next >= starts.size) return null
    return intArrayOf(lineStart(next), lineEnd(next))
  }

  private fun linePreceding(to: Int): IntArray? {
    val current = lineFor(to)
    val previous = if (lineEnd(current) == to) current else current - 1
    if (previous < 0) return null
    return intArrayOf(lineStart(previous), lineEnd(previous))
  }

  // MARK: - Characters and words

  private val characters: BreakIterator by lazy { BreakIterator.getCharacterInstance(Locale.getDefault()).also { it.setText(text) } }
  private val words: BreakIterator by lazy { BreakIterator.getWordInstance(Locale.getDefault()).also { it.setText(text) } }

  private fun breakFollowing(iterator: BreakIterator, from: Int): IntArray? {
    var start = from
    while (!iterator.isBoundary(start)) {
      start = iterator.following(start)
      if (start == BreakIterator.DONE) return null
    }
    val end = iterator.following(start)
    if (end == BreakIterator.DONE) return null
    return intArrayOf(start, end)
  }

  private fun breakPreceding(iterator: BreakIterator, to: Int): IntArray? {
    var end = to
    while (!iterator.isBoundary(end)) {
      end = iterator.preceding(end)
      if (end == BreakIterator.DONE) return null
    }
    val start = iterator.preceding(end)
    if (start == BreakIterator.DONE) return null
    return intArrayOf(start, end)
  }

  private fun wordFollowing(from: Int): IntArray? {
    var start = from
    while (!isLetterOrDigit(start) && !isWordStart(start)) {
      start = words.following(start)
      if (start == BreakIterator.DONE) return null
    }
    val end = words.following(start)
    if (end == BreakIterator.DONE || !isWordEnd(end)) return null
    return intArrayOf(start, end)
  }

  private fun wordPreceding(to: Int): IntArray? {
    var end = to
    while (end > 0 && !isLetterOrDigit(end - 1) && !isWordEnd(end)) {
      end = words.preceding(end)
      if (end == BreakIterator.DONE) return null
    }
    val start = words.preceding(end)
    if (start == BreakIterator.DONE || !isWordStart(start)) return null
    return intArrayOf(start, end)
  }

  private fun isWordStart(i: Int) = isLetterOrDigit(i) && (i == 0 || !isLetterOrDigit(i - 1))

  private fun isWordEnd(i: Int) = i > 0 && isLetterOrDigit(i - 1) && (i == text.length || !isLetterOrDigit(i))

  private fun isLetterOrDigit(i: Int) = i >= 0 && i < text.length && Character.isLetterOrDigit(text.codePointAt(i))

  // MARK: - Paragraphs

  private fun paragraphFollowing(from: Int): IntArray? {
    var start = from
    while (start < text.length && text[start] == '\n' && !isParagraphStart(start)) start++
    if (start >= text.length) return null
    var end = start + 1
    while (end < text.length && !isParagraphEnd(end)) end++
    return intArrayOf(start, end)
  }

  private fun paragraphPreceding(to: Int): IntArray? {
    var end = to
    while (end > 0 && text[end - 1] == '\n' && !isParagraphEnd(end)) end--
    if (end <= 0) return null
    var start = end - 1
    while (start > 0 && !isParagraphStart(start)) start--
    return intArrayOf(start, end)
  }

  private fun isParagraphStart(i: Int) = text[i] != '\n' && (i == 0 || text[i - 1] == '\n')

  private fun isParagraphEnd(i: Int) = i > 0 && text[i - 1] != '\n' && (i == text.length || text[i] == '\n')

  companion object {
    const val GRANULARITIES = AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER or
        AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD or AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE or
        AccessibilityNodeInfo.MOVEMENT_GRANULARITY_PARAGRAPH
    val EMPTY = StreamTextReading("", IntArray(0))
  }
}
