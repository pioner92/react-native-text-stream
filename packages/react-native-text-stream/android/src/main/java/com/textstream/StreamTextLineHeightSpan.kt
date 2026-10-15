package com.textstream

import android.graphics.Paint.FontMetricsInt
import android.text.style.LineHeightSpan
import kotlin.math.ceil
import kotlin.math.floor

/**
 * Every line exactly `lineHeight` px, the half-leading above and below, as <Text>'s
 * CustomLineHeightSpan does: lines with fallback fonts (emoji, CJK) keep the pitch. It is not an
 * UpdateLayout span, so growing with the appended text doesn't re-wrap what came before.
 */
internal class StreamTextLineHeightSpan(height: Float) : LineHeightSpan {
  val lineHeight: Int = ceil(height.toDouble()).toInt()

  override fun chooseHeight(text: CharSequence, start: Int, end: Int, spanstartv: Int, v: Int, fm: FontMetricsInt) {
    // Explicit text pitch must never shrink an image or table attachment.
    if (text is android.text.Spanned && text.getSpans(start, end, android.text.style.ReplacementSpan::class.java).isNotEmpty()) return
    val leading = lineHeight - (-fm.ascent + fm.descent)
    fm.ascent -= ceil(leading / 2.0f).toInt()
    fm.descent += floor(leading / 2.0f).toInt()
    // No font padding above the first line or below the last, as <Text>'s span (an empty line after
    // a final \n has no span and keeps it).
    fm.top = fm.ascent
    fm.bottom = fm.descent
  }
}
