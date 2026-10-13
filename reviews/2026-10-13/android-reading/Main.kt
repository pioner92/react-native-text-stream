package com.textstream

import android.view.accessibility.AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD

fun main() {
  val text = "\uD801\uDC00" // U+10400 DESERET CAPITAL LETTER LONG I, a Unicode letter
  val reading = StreamTextReading(text, intArrayOf(0))
  println("forward=" + (reading.following(MOVEMENT_GRANULARITY_WORD, 0)?.contentToString() ?: "null"))
  println("backward=" + (reading.preceding(MOVEMENT_GRANULARITY_WORD, text.length)?.contentToString() ?: "null"))
}
