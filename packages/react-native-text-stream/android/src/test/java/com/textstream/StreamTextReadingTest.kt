package com.textstream

import android.view.accessibility.AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER
import android.view.accessibility.AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE
import android.view.accessibility.AccessibilityNodeInfo.MOVEMENT_GRANULARITY_PARAGRAPH
import android.view.accessibility.AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD
import org.junit.Assert.assertEquals
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StreamTextReadingTest {
  /** The segments TalkBack moves over from the start to the end, then back. */
  private fun walk(reading: StreamTextReading, granularity: Int): Pair<List<String>, List<String>> {
    val forward = ArrayList<String>()
    var cursor = 0
    while (true) {
      val range = reading.following(granularity, cursor) ?: break
      forward.add(reading.text.substring(range[0], range[1]))
      cursor = range[1]
    }
    val backward = ArrayList<String>()
    cursor = reading.text.length
    while (true) {
      val range = reading.preceding(granularity, cursor) ?: break
      backward.add(reading.text.substring(range[0], range[1]))
      cursor = range[0]
    }
    return forward to backward
  }

  private val text = "One two,\nthree four five\n\nsix"
  // Wrapped lines: "One two,\n" | "three four " | "five\n" | "\n" | "six"
  private val reading = StreamTextReading(text, intArrayOf(0, 9, 20, 25, 26))

  @Test fun lines() {
    val (forward, backward) = walk(reading, MOVEMENT_GRANULARITY_LINE)
    assertEquals(listOf("One two,\n", "three four ", "five\n", "\n", "six"), forward)
    assertEquals(forward.reversed(), backward)
  }

  @Test fun words() {
    val (forward, backward) = walk(reading, MOVEMENT_GRANULARITY_WORD)
    assertEquals(listOf("One", "two", "three", "four", "five", "six"), forward)
    assertEquals(forward.reversed(), backward)
  }

  @Test fun paragraphs() {
    val (forward, backward) = walk(reading, MOVEMENT_GRANULARITY_PARAGRAPH)
    assertEquals(listOf("One two,", "three four five", "six"), forward)
    assertEquals(forward.reversed(), backward)
  }

  @Test fun characters() {
    val emoji = StreamTextReading("a🚀b", intArrayOf(0))
    assertEquals(listOf("a", "🚀", "b"), walk(emoji, MOVEMENT_GRANULARITY_CHARACTER).first)
  }

  @Test fun wordsKeepSupplementaryLettersAndDigitsWholeAtUtf16Offsets() {
    // Deseret is supplementary-plane and has letters and digits.  Accessibility reports UTF-16
    // indices, including a cursor between surrogate halves, so every range must retain the pair.
    val letter = "\uD801\uDC00" // U+10400 DESERET CAPITAL LETTER LONG I
    val digit = "\uD801\uDCA5" // U+104A5 OSMANYA DIGIT FIVE
    val text = "$letter$digit 7$letter"
    val reading = StreamTextReading(text, intArrayOf(0))

    assertArrayEquals(intArrayOf(0, 4), reading.following(MOVEMENT_GRANULARITY_WORD, 0))
    assertArrayEquals(intArrayOf(0, 4), reading.following(MOVEMENT_GRANULARITY_WORD, 1))
    assertArrayEquals(intArrayOf(0, 2), reading.preceding(MOVEMENT_GRANULARITY_WORD, 1))
    assertArrayEquals(intArrayOf(0, 4), reading.preceding(MOVEMENT_GRANULARITY_WORD, 4))
    assertArrayEquals(intArrayOf(5, 8), reading.following(MOVEMENT_GRANULARITY_WORD, 5))
    assertArrayEquals(intArrayOf(5, 8), reading.preceding(MOVEMENT_GRANULARITY_WORD, 7))
  }

  @Test fun aTailReadsItsEllipsisWithTheFirstLine() {
    val tail = StreamTextReading("…end of a line\nlast", intArrayOf(1, 15))
    assertEquals(listOf("…end of a line\n", "last"), walk(tail, MOVEMENT_GRANULARITY_LINE).first)
  }

  @Test fun nothingPastTheEnds() {
    assertNull(reading.following(MOVEMENT_GRANULARITY_LINE, text.length))
    assertNull(reading.preceding(MOVEMENT_GRANULARITY_LINE, 0))
    assertNull(StreamTextReading.EMPTY.following(MOVEMENT_GRANULARITY_LINE, -1))
  }
}
