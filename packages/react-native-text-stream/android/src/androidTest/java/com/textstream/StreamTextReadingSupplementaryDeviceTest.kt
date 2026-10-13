package com.textstream

import android.view.accessibility.AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertArrayEquals
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Device regression probe for TalkBack's word-navigation contract.
 *
 * U+10400 is a Deseret letter encoded as a UTF-16 surrogate pair.  Android accessibility APIs
 * still report UTF-16 indices, so the one-letter word must span [0, 2] in both directions.
 */
@RunWith(AndroidJUnit4::class)
class StreamTextReadingSupplementaryDeviceTest {
  private val deseretLetter = "\uD801\uDC00"

  @Test
  fun nextWordReadsOneSupplementaryLetterAsOneWord() {
    val reading = StreamTextReading(deseretLetter, intArrayOf(0))

    assertArrayEquals(
        "TalkBack next-word must retain the complete surrogate pair",
        intArrayOf(0, 2),
        reading.following(MOVEMENT_GRANULARITY_WORD, 0),
    )
  }

  @Test
  fun previousWordReadsOneSupplementaryLetterAsOneWord() {
    val reading = StreamTextReading(deseretLetter, intArrayOf(0))

    assertArrayEquals(
        "TalkBack previous-word must retain the complete surrogate pair",
        intArrayOf(0, 2),
        reading.preceding(MOVEMENT_GRANULARITY_WORD, 2),
    )
  }

  @Test
  fun mixedSupplementaryWordKeepsPairsWhenTheCursorIsInsideOne() {
    val osmanyaDigit = "\uD801\uDCA5" // U+104A5, a supplementary-plane decimal digit.
    val reading = StreamTextReading("${deseretLetter}${osmanyaDigit} 7${deseretLetter}", intArrayOf(0))

    assertArrayEquals(intArrayOf(0, 4), reading.following(MOVEMENT_GRANULARITY_WORD, 1))
    assertArrayEquals(intArrayOf(0, 4), reading.preceding(MOVEMENT_GRANULARITY_WORD, 3))
    assertArrayEquals(intArrayOf(5, 8), reading.following(MOVEMENT_GRANULARITY_WORD, 5))
    assertArrayEquals(intArrayOf(5, 8), reading.preceding(MOVEMENT_GRANULARITY_WORD, 7))
  }
}
