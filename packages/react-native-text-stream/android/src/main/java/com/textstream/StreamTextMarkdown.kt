package com.textstream

import android.graphics.Typeface
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.TextPaint
import android.text.style.CharacterStyle
import android.text.style.LeadingMarginSpan
import android.text.style.RelativeSizeSpan
import android.text.style.StrikethroughSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import com.facebook.proguard.annotations.DoNotStrip

/**
 * The compact result of the native MD4C parser.  All positions are UTF-16 offsets, so Kotlin can
 * apply them directly without an offset conversion on the UI thread.
 */
@DoNotStrip
internal class StreamTextMarkdown @DoNotStrip constructor(
    val text: String,
    private val starts: IntArray,
    private val lengths: IntArray,
    private val flags: IntArray,
    private val headings: IntArray,
    private val indents: IntArray,
    private val urls: Array<String?>,
) {
  /**
   * First UTF-16 offset whose visible text or semantic run differs from [previous].  The parser
   * returns a whole document, but this lets the view retain DynamicLayout's already-valid prefix.
   */
  fun changedStartFrom(previous: StreamTextMarkdown?): Int {
    if (previous == null) return 0
    val common = minOf(text.length, previous.text.length)
    var at = 0
    while (at < common && text[at] == previous.text[at]) at++
    var changed = at
    val count = minOf(starts.size, previous.starts.size)
    for (i in 0 until count) {
      if (starts[i] != previous.starts[i] || lengths[i] != previous.lengths[i] ||
          flags[i] != previous.flags[i] || headings[i] != previous.headings[i] ||
          indents[i] != previous.indents[i] || urls[i] != previous.urls[i]) {
        changed = minOf(changed, starts[i], previous.starts[i])
        break
      }
    }
    if (starts.size != previous.starts.size) {
      changed = minOf(changed, if (count < starts.size) starts[count] else previous.starts[count])
    }
    return changed.coerceIn(0, minOf(text.length, previous.text.length))
  }

  fun styled(@Suppress("UNUSED_PARAMETER") baseColor: Int): CharSequence {
    val out = SpannableStringBuilder(text)
    for (i in starts.indices) {
      val start = starts[i].coerceIn(0, out.length)
      val end = (start + lengths[i]).coerceIn(start, out.length)
      if (start == end) continue
      val bits = flags[i]
      val spanFlags = Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
      val bold = bits and BOLD != 0
      val italic = bits and ITALIC != 0
      if (bold || italic) out.setSpan(StyleSpan(if (bold && italic) Typeface.BOLD_ITALIC else if (bold) Typeface.BOLD else Typeface.ITALIC), start, end, spanFlags)
      if (bits and STRIKE != 0) out.setSpan(StrikethroughSpan(), start, end, spanFlags)
      if (bits and CODE != 0) {
        out.setSpan(TypefaceSpan("monospace"), start, end, spanFlags)
        out.setSpan(MarkdownCodeBackgroundSpan(24), start, end, spanFlags)
      }
      if (bits and CODE_BLOCK != 0) {
        out.setSpan(TypefaceSpan("monospace"), start, end, spanFlags)
        out.setSpan(MarkdownCodeBackgroundSpan(18), start, end, spanFlags)
        out.setSpan(LeadingMarginSpan.Standard(dp(24)), start, end, spanFlags)
      }
      if (bits and QUOTE != 0) {
        out.setSpan(LeadingMarginSpan.Standard(dp(18)), start, end, spanFlags)
        out.setSpan(MarkdownQuoteSpan(), start, end, spanFlags)
      }
      urls.getOrNull(i)?.takeIf { bits and LINK != 0 && isSafeUrl(it) }?.let { out.setSpan(MarkdownUrlSpan(it), start, end, spanFlags) }
      when (headings.getOrElse(i) { 0 }) {
        1 -> { out.setSpan(RelativeSizeSpan(1.55f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        2 -> { out.setSpan(RelativeSizeSpan(1.38f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        3 -> { out.setSpan(RelativeSizeSpan(1.22f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        4 -> { out.setSpan(RelativeSizeSpan(1.12f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        in 5..6 -> out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags)
      }
      val indent = indents.getOrElse(i) { 0 }
      if (indent > 0) out.setSpan(LeadingMarginSpan.Standard(dp(indent.coerceAtMost(8) * 16)), start, end, spanFlags)
    }
    return out
  }

  /**
   * These spans derive their color from the paint at draw time. Prepared Markdown layouts can
   * therefore survive a themed default color or a color-prop change without a second parse or
   * StaticLayout build.
   */
  private class MarkdownCodeBackgroundSpan(private val alpha: Int) : CharacterStyle() {
    override fun updateDrawState(paint: TextPaint) {
      paint.bgColor = (paint.color and 0x00ffffff) or (alpha.coerceIn(0, 255) shl 24)
    }
  }

  private class MarkdownQuoteSpan : CharacterStyle() {
    override fun updateDrawState(paint: TextPaint) {
      val from = paint.color
      val to = 0xff808080.toInt()
      val amount = 0.28f
      fun component(shift: Int) = (((from ushr shift) and 0xff) * (1f - amount) + ((to ushr shift) and 0xff) * amount).toInt()
      paint.color = (component(24) shl 24) or (component(16) shl 16) or (component(8) shl 8) or component(0)
    }
  }

  private class MarkdownUrlSpan(url: String) : URLSpan(url) {
    override fun updateDrawState(paint: TextPaint) {
      paint.isUnderlineText = true
    }
  }

  companion object {
    fun plain(text: String) = StreamTextMarkdown(text, IntArray(0), IntArray(0), IntArray(0), IntArray(0), IntArray(0), emptyArray())
    const val BOLD = 1
    const val ITALIC = 2
    const val STRIKE = 4
    const val CODE = 8
    const val CODE_BLOCK = 16
    const val QUOTE = 32
    const val LINK = 64

    private fun dp(value: Int) = (value * android.content.res.Resources.getSystem().displayMetrics.density + 0.5f).toInt()
    private fun isSafeUrl(url: String): Boolean {
      val scheme = url.substringBefore(':', "").lowercase()
      return scheme == "http" || scheme == "https" || scheme == "mailto"
    }
  }
}
