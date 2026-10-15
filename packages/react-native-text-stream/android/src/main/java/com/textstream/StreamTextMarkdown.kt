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
    internal val blocks: Array<RichBlock>,
) {
  /** Kept as a separate overload because JNI resolves constructors by their exact descriptor. */
  @DoNotStrip
  constructor(
      text: String, starts: IntArray, lengths: IntArray, flags: IntArray, headings: IntArray,
      indents: IntArray, urls: Array<String?>,
  ) : this(text, starts, lengths, flags, headings, indents, urls, emptyArray())

  @DoNotStrip
  internal class RichBlock @DoNotStrip constructor(
      val kind: Int,
      val start: Int,
      val rowCount: Int,
      val columnCount: Int,
      val cells: Array<RichCell>?,
      val url: String?,
      val alt: String?,
  )

  @DoNotStrip
  internal class RichCell @DoNotStrip constructor(
      val text: String,
      val starts: IntArray,
      val lengths: IntArray,
      val flags: IntArray,
      val headings: IntArray,
      val indents: IntArray,
      val urls: Array<String?>,
      val header: Boolean,
      val alignment: Int,
  )

  internal val hasRichBlocks: Boolean get() = blocks.isNotEmpty()
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
    val blockCount = minOf(blocks.size, previous.blocks.size)
    for (i in 0 until blockCount) {
      if (!blocks[i].sameAs(previous.blocks[i])) { changed = minOf(changed, blocks[i].start, previous.blocks[i].start); break }
    }
    if (blocks.size != previous.blocks.size) changed = minOf(changed, if (blockCount < blocks.size) blocks[blockCount].start else previous.blocks[blockCount].start)
    return changed.coerceIn(0, minOf(text.length, previous.text.length))
  }

  private fun RichBlock.sameAs(other: RichBlock): Boolean {
    if (kind != other.kind || start != other.start || rowCount != other.rowCount || columnCount != other.columnCount || url != other.url || alt != other.alt) return false
    val a = cells; val b = other.cells
    if (a == null || b == null) return a === b
    if (a.size != b.size) return false
    return a.indices.all { a[it].sameAs(b[it]) }
  }
  private fun RichCell.sameAs(other: RichCell) = text == other.text && header == other.header && alignment == other.alignment &&
      starts.contentEquals(other.starts) && lengths.contentEquals(other.lengths) && flags.contentEquals(other.flags) &&
      headings.contentEquals(other.headings) && indents.contentEquals(other.indents) && urls.contentEquals(other.urls)

  /** Bounded expansion using absolute document positions, including a tail starting after earlier
   * attachments. Rich accessibility lines follow the expanded text, never stale glyph offsets. */
  fun accessibleReading(from: Int, limit: Int): StreamTextReading {
    val out = StringBuilder(minOf(limit, text.length))
    fun append(value: String) {
      var count = minOf(value.length, limit - out.length)
      if (count > 0 && count < value.length && Character.isHighSurrogate(value[count - 1])) count--
      out.append(value, 0, count)
    }
    var position = from.coerceIn(0, text.length)
    if (position > 0) append("…")
    var low = 0; var high = blocks.size
    while (low < high) {
      val mid = (low + high) ushr 1
      if (blocks[mid].start < position) low = mid + 1 else high = mid
    }
    var index = low
    while (position < text.length && out.length < limit) {
      val block = blocks.getOrNull(index)
      if (block != null && block.start == position) {
        if (block.kind == BLOCK_TABLE) {
          append("table\n")
          val cells = block.cells ?: emptyArray()
          for (i in cells.indices) {
            if (out.length >= limit) break
            append(cells[i].text)
            append(if ((i + 1) % block.columnCount.coerceAtLeast(1) == 0) "\n" else "; ")
          }
        } else { append("image "); append(block.alt.orEmpty()) }
        position++; index++
      } else {
        val c = text[position++]
        if (Character.isHighSurrogate(c) && position < text.length && Character.isLowSurrogate(text[position])) {
          if (limit - out.length < 2) break
          out.append(c).append(text[position++])
        } else out.append(c)
      }
    }
    val expanded = out.toString()
    val lines = ArrayList<Int>(); lines.add(0)
    for (i in expanded.indices) if (expanded[i] == '\n' && i + 1 < expanded.length) lines.add(i + 1)
    return StreamTextReading(expanded, lines.toIntArray())
  }

  fun styled(@Suppress("UNUSED_PARAMETER") baseColor: Int, width: Int = 0, style: StreamTextStyle = StreamTextStyle()): CharSequence {
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
      tokenSpan(bits)?.let { out.setSpan(it, start, end, spanFlags) }
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
    if (width > 0) for (block in blocks) {
      val start = block.start.coerceIn(0, out.length)
      if (start >= out.length) continue
      when (block.kind) {
        BLOCK_TABLE -> out.setSpan(MarkdownTableSpan(block, width, style), start, start + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        BLOCK_IMAGE -> out.setSpan(MarkdownImageSpan(block.url.orEmpty(), block.alt.orEmpty(), width), start, start + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
      }
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

  /** Syntax colors are paint-relative, so theme/color changes do not invalidate parsed Markdown. */
  private class MarkdownTokenSpan(private val kind: Int) : CharacterStyle() {
    override fun updateDrawState(paint: TextPaint) {
      val base = paint.color
      val target = when (kind) {
        TOKEN_KEYWORD -> 0xff8b5cf6.toInt()
        TOKEN_STRING -> 0xff16a34a.toInt()
        TOKEN_COMMENT -> 0xff6b7280.toInt()
        TOKEN_NUMBER -> 0xffd97706.toInt()
        else -> 0xff0284c7.toInt()
      }
      // Preserve a readable relationship to the active text color rather than baking a theme.
      fun c(shift: Int) = (((base ushr shift and 0xff) * 0.35f) + ((target ushr shift and 0xff) * 0.65f)).toInt()
      paint.color = (base and -0x1000000) or (c(16) shl 16) or (c(8) shl 8) or c(0)
    }
  }

  private fun tokenSpan(bits: Int): CharacterStyle? = when {
    bits and TOKEN_KEYWORD != 0 -> MarkdownTokenSpan(TOKEN_KEYWORD)
    bits and TOKEN_STRING != 0 -> MarkdownTokenSpan(TOKEN_STRING)
    bits and TOKEN_COMMENT != 0 -> MarkdownTokenSpan(TOKEN_COMMENT)
    bits and TOKEN_NUMBER != 0 -> MarkdownTokenSpan(TOKEN_NUMBER)
    bits and TOKEN_TYPE != 0 -> MarkdownTokenSpan(TOKEN_TYPE)
    else -> null
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
    const val TOKEN_KEYWORD = 1 shl 16
    const val TOKEN_STRING = 1 shl 17
    const val TOKEN_COMMENT = 1 shl 18
    const val TOKEN_NUMBER = 1 shl 19
    const val TOKEN_TYPE = 1 shl 20
    const val BLOCK_TABLE = 1
    const val BLOCK_IMAGE = 2

    private fun dp(value: Int) = (value * android.content.res.Resources.getSystem().displayMetrics.density + 0.5f).toInt()
    private fun isSafeUrl(url: String): Boolean {
      val scheme = url.substringBefore(':', "").lowercase()
      return scheme == "http" || scheme == "https" || scheme == "mailto"
    }
  }
}
