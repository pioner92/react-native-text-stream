package com.textstream

import android.content.res.AssetManager
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Build
import android.text.DynamicLayout
import android.text.Layout
import android.text.Spannable
import android.text.SpannableString
import android.text.Spanned
import android.text.StaticLayout
import android.text.TextDirectionHeuristics
import android.text.TextPaint
import com.facebook.react.uimanager.PixelUtil
import com.facebook.react.views.text.ReactTypefaceUtils
import kotlin.math.ceil

/**
 * The props that place the lines (not the color, which is only drawn), and the one place that
 * turns them into a layout: the streaming DynamicLayout and a StaticLayout prepared ahead must
 * wrap and size the same text identically. Line metrics are <Text>'s on Android.
 */
data class StreamTextStyle(
    val fontSize: Float = 15f,
    val fontFamily: String? = null,
    val fontWeight: String? = null,
    val lineHeight: Float = 0f,
    val letterSpacing: Float = 0f,
    val textAlign: String? = null,
) {
  /** A new paint (layouts keep theirs: its color is set when drawing). Any thread. */
  fun paint(assets: AssetManager?): TextPaint {
    val paint = TextPaint(Paint.ANTI_ALIAS_FLAG)
    // Whole pixels, and letter spacing in em of them, as <Text> (TextAttributeProps) has them.
    val textSize = ceil(PixelUtil.toPixelFromDIP(fontSize).toDouble()).toFloat()
    paint.textSize = textSize
    paint.letterSpacing = if (textSize > 0) PixelUtil.toPixelFromDIP(letterSpacing) / textSize else 0f
    val weight = ReactTypefaceUtils.parseFontWeight(fontWeight)
    paint.typeface =
        if (assets != null) ReactTypefaceUtils.applyStyles(null, Typeface.NORMAL, weight, fontFamily, assets)
        else Typeface.create(Typeface.DEFAULT, if (weight >= 600) Typeface.BOLD else Typeface.NORMAL)
    return paint
  }

  private val alignment: Layout.Alignment
    get() = when (textAlign) {
      "right" -> Layout.Alignment.ALIGN_OPPOSITE
      "center" -> Layout.Alignment.ALIGN_CENTER
      else -> Layout.Alignment.ALIGN_NORMAL
    }

  /** Sets (or removes) the line height span over the whole text; it grows with appends. */
  fun applyLineHeight(text: Spannable) {
    for (span in text.getSpans(0, text.length, StreamTextLineHeightSpan::class.java)) text.removeSpan(span)
    if (lineHeight > 0) {
      text.setSpan(StreamTextLineHeightSpan(PixelUtil.toPixelFromDIP(lineHeight)), 0, text.length, Spanned.SPAN_INCLUSIVE_INCLUSIVE)
    }
  }

  /** The layout an append re-wraps only from the paragraph it touched. */
  fun dynamicLayout(text: Spannable, paint: TextPaint, width: Int): DynamicLayout {
    applyLineHeight(text)
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
      @Suppress("DEPRECATION")
      return DynamicLayout(text, paint, width, alignment, 1f, 0f, true)
    }
    return DynamicLayout.Builder.obtain(text, paint, width)
        .setAlignment(alignment)
        // <Text>'s default includeFontPadding: only where no span sets the line's top or bottom.
        .setIncludePad(true)
        .setTextDirection(TextDirectionHeuristics.FIRSTSTRONG_LTR)
        // Greedy wrapping, as TextKit: an append never re-wraps the lines above it.
        .setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE)
        .setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE)
        // Lines with emoji or CJK from fallback fonts are as tall as those need, as in <Text>.
        .setUseLineSpacingFromFallbacks(true)
        .setJustificationMode(justification)
        .build()
  }

  /** The same lines as [dynamicLayout] for a finished text, laid out ahead (any thread). */
  fun staticLayout(text: String, paint: TextPaint, width: Int): StaticLayout {
    val spannable = SpannableString(text)
    applyLineHeight(spannable)
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
      @Suppress("DEPRECATION")
      return StaticLayout(spannable, paint, width, alignment, 1f, 0f, true)
    }
    return StaticLayout.Builder.obtain(spannable, 0, spannable.length, paint, width)
        .setAlignment(alignment)
        .setIncludePad(true)
        .setTextDirection(TextDirectionHeuristics.FIRSTSTRONG_LTR)
        .setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE)
        .setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE)
        .setUseLineSpacingFromFallbacks(true)
        .setJustificationMode(justification)
        .build()
  }

  private val justification: Int
    get() = if (textAlign == "justify" && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      Layout.JUSTIFICATION_MODE_INTER_WORD
    } else {
      0 // JUSTIFICATION_MODE_NONE
    }
}
