package com.textstream

import android.graphics.Typeface
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.TextPaint
import android.text.style.CharacterStyle
import android.text.style.LeadingMarginSpan
import android.text.style.LineHeightSpan
import android.text.style.LineBackgroundSpan
import android.text.style.RelativeSizeSpan
import android.text.style.ReplacementSpan
import android.text.style.StrikethroughSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import com.facebook.proguard.annotations.DoNotStrip
import com.facebook.react.uimanager.PixelUtil
import kotlin.math.ceil
import kotlin.math.floor

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
   * A code-block surface and its header span cover a complete contiguous fence, rather than one
   * parser token. A suffix update beginning in a later code paragraph must therefore replace the
   * entire fence: otherwise Android retains a clipped old surface/header and adds a second span
   * for the suffix, which can double its line-height or paint.
   */
  internal fun codeBlockClusterStart(offset: Int): Int =
      codeBlockClusterStart(text, starts, lengths, flags, offset)
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
  /**
   * Expands semantics inside a source range. [limit] caps produced accessibility text independently
   * from [sourceEnd]: a task marker occupies three source characters but expands to a phrase.
   */
  fun accessibleReading(from: Int, limit: Int, sourceEnd: Int = text.length): StreamTextReading {
    val out = StringBuilder(minOf(limit, text.length))
    fun append(value: String) {
      var count = minOf(value.length, limit - out.length)
      if (count > 0 && count < value.length && Character.isHighSurrogate(value[count - 1])) count--
      out.append(value, 0, count)
    }
    var position = from.coerceIn(0, text.length)
    val end = sourceEnd.coerceIn(position, text.length)
    if (position > 0) append("…")
    var low = 0; var high = blocks.size
    while (low < high) {
      val mid = (low + high) ushr 1
      if (blocks[mid].start < position) low = mid + 1 else high = mid
    }
    var index = low
    // Task markers are derived once from parser-confirmed marker runs, then consumed in document
    // order. Accessibility gets the semantic control state rather than literal Markdown syntax.
    val markers = taskMarkers(text, starts, lengths, flags)
    var markerIndex = 0
    while (markerIndex < markers.size && markers[markerIndex].start < position) markerIndex++
    while (position < end && out.length < limit) {
      val marker = markers.getOrNull(markerIndex)
      if (marker != null && marker.start == position) {
        append(if (marker.checked) "Checked task" else "Unchecked task")
        position += 3
        markerIndex++
        continue
      }
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
    val codeBlockMargins = codeBlockMarginRanges(text, starts, lengths, flags)
    val paragraphDecorations = paragraphDecorations(text, starts, lengths, flags, indents)
    val taskMarkers = taskMarkers(text, starts, lengths, flags)
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
      if (bits and CODE != 0 && bits and CODE_BLOCK == 0) {
        out.setSpan(TypefaceSpan("monospace"), start, end, spanFlags)
        out.setSpan(MarkdownCodeBackgroundSpan(24), start, end, spanFlags)
      }
      if (bits and CODE_BLOCK != 0) {
        // The block surface is drawn once per visual line below. Per-token backgrounds create
        // visible stair-steps and extra draw work for syntax-highlighted fences.
        out.setSpan(TypefaceSpan("monospace"), start, end, spanFlags)
      }
      tokenSpan(bits)?.let { out.setSpan(it, start, end, spanFlags) }
      urls.getOrNull(i)?.takeIf { bits and LINK != 0 && isSafeUrl(it) }?.let { out.setSpan(MarkdownUrlSpan(it), start, end, spanFlags) }
      when (headings.getOrElse(i) { 0 }) {
        1 -> { out.setSpan(RelativeSizeSpan(1.55f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        2 -> { out.setSpan(RelativeSizeSpan(1.38f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        3 -> { out.setSpan(RelativeSizeSpan(1.22f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        4 -> { out.setSpan(RelativeSizeSpan(1.12f), start, end, spanFlags); out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags) }
        in 5..6 -> out.setSpan(StyleSpan(Typeface.BOLD), start, end, spanFlags)
      }
    }
    // Syntax-token runs share CODE_BLOCK. Add each paragraph margin once, outside the per-token
    // loop, so a token-dense or very long line remains linear to format.
    val paragraphLanguages = codeLanguageLabels(codeBlockMargins)
    for ((start, end) in codeBlockMargins) {
      // Margin spans must stay paragraph-scoped. The surface itself is attached once below for
      // each contiguous block, which keeps its rounded outer corners seamless across lines.
      out.setSpan(LeadingMarginSpan.Standard(dp(8)), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }
    for (section in codeBlockSections(codeBlockMargins, paragraphLanguages, text)) {
      out.setSpan(MarkdownCodeBlockSpan(section.start, section.end, section.label), section.start, section.end,
          Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
      out.setSpan(MarkdownCodeHeaderSpan(section.start,
          if (style.lineHeight > 0f) ceil(PixelUtil.toPixelFromDIP(style.lineHeight).toDouble()).toInt() else 0),
          section.start, section.firstParagraphEnd,
          Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }
    for (marker in taskMarkers) out.setSpan(MarkdownTaskSpan(marker.checked), marker.start, marker.start + 3, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    for (decoration in paragraphDecorations) {
      if (decoration.quote) {
        out.setSpan(LeadingMarginSpan.Standard(dp(18)), decoration.start, decoration.end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(MarkdownQuoteSpan(), decoration.start, decoration.end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
      }
      if (decoration.indent > 0) out.setSpan(
          LeadingMarginSpan.Standard(dp(decoration.indent.coerceAtMost(8) * 16)),
          decoration.start, decoration.end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }
    if (width > 0) for (block in blocks) {
      val start = block.start.coerceIn(0, out.length)
      if (start >= out.length) continue
      when (block.kind) {
        BLOCK_TABLE -> out.setSpan(MarkdownTableSpan(block, width, style), start, start + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        // A streaming image has a stable attachment as soon as its `![alt]` prefix is
        // recognized. Its URL remains null until the closing `)` arrives. Keeping that state
        // explicit prevents a partial URL from reaching the network layer and lets the span be
        // replaced in-place when the real image becomes loadable.
        BLOCK_IMAGE, BLOCK_IMAGE_PENDING -> out.setSpan(
            MarkdownImageSpan(block.url, block.alt.orEmpty(), width), start, start + 1,
            Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
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

  /** Draws a single code surface per visual line; no per-token object or paint allocation. */
  private class MarkdownCodeBlockSpan(
      private val blockStart: Int, private val blockEnd: Int, private val label: String?,
  ) : LineBackgroundSpan {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bounds = RectF()
    private val path = Path()
    private val cornerRadii = FloatArray(8)
    override fun drawBackground(canvas: Canvas, paint: Paint, left: Int, right: Int, top: Int, baseline: Int,
        bottom: Int, text: CharSequence, start: Int, end: Int, lineNum: Int) {
      this.paint.color = (paint.color and 0x00ffffff) or 0x24000000
      bounds.set(left.toFloat(), top.toFloat(), right.toFloat(), bottom.toFloat())
      val radius = dp(4).toFloat()
      val first = start == blockStart
      val last = end >= blockEnd
      if (first || last) {
        path.reset()
        val topRadius = if (first) radius else 0f
        val bottomRadius = if (last) radius else 0f
        cornerRadii[0] = topRadius; cornerRadii[1] = topRadius
        cornerRadii[2] = topRadius; cornerRadii[3] = topRadius
        cornerRadii[4] = bottomRadius; cornerRadii[5] = bottomRadius
        cornerRadii[6] = bottomRadius; cornerRadii[7] = bottomRadius
        path.addRoundRect(bounds, cornerRadii, Path.Direction.CW)
        canvas.drawPath(path, this.paint)
      } else canvas.drawRect(bounds, this.paint)
      if (label != null && first) {
        this.paint.color = (paint.color and 0x00ffffff) or 0xb0000000.toInt()
        this.paint.textSize = paint.textSize.coerceAtMost(dp(10).toFloat())
        // The header span below reserves this band only on the section's first line.
        canvas.drawText(label, left + dp(8).toFloat(), top + dp(4).toFloat() + this.paint.textSize, this.paint)
      }
    }
  }

  /** Adds a compact language-header band only at the start of a fenced-language section. */
  /**
   * Owns the first fenced paragraph's metrics. It overwrites metrics on every visual line, so
   * StaticLayout cannot carry a header-adjusted ascent into a wrapped continuation. The ordinary
   * line-height span recognizes this span and yields for this paragraph, independent of Android's
   * span invocation order.
   */
  internal class MarkdownCodeHeaderSpan(private val headerOffset: Int, private val lineHeight: Int) : LineHeightSpan.WithDensity {
    override fun chooseHeight(text: CharSequence, start: Int, end: Int, spanStartV: Int, v: Int,
        fm: Paint.FontMetricsInt, paint: TextPaint) = setMetrics(start, fm, paint)

    override fun chooseHeight(text: CharSequence, start: Int, end: Int, spanStartV: Int, v: Int,
        fm: Paint.FontMetricsInt) = setMetrics(start, fm, null)

    private fun setMetrics(lineStart: Int, fm: Paint.FontMetricsInt, paint: TextPaint?) {
      if (paint != null) {
        paint.getFontMetricsInt(fm)
        if (lineHeight > 0) {
          val leading = lineHeight - (-fm.ascent + fm.descent)
          fm.ascent -= ceil(leading / 2.0f).toInt()
          fm.descent += floor(leading / 2.0f).toInt()
        }
        fm.top = fm.ascent
        fm.bottom = fm.descent
      }
      if (lineStart == headerOffset) {
        // 4dp label top padding + its 10dp cap + 6dp breathing room before the first glyph.
        // StaticLayout otherwise carries an inflated ascent into subsequent visual wraps while
        // the paragraph-scoped header span remains active.
        val header = dp(20)
        fm.ascent -= header
        fm.top -= header
      }
    }
  }

  private class MarkdownTaskSpan(private val checked: Boolean) : ReplacementSpan() {
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = dp(1).toFloat() }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int = dp(20)
    override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float, top: Int, y: Int, bottom: Int, paint: Paint) {
      val size = dp(14).toFloat(); val left = x + dp(2); val boxTop = y - paint.textSize * .72f
      stroke.color = paint.color
      if (checked) { fill.color = paint.color; canvas.drawRoundRect(left, boxTop, left + size, boxTop + size, dp(3).toFloat(), dp(3).toFloat(), fill) }
      else canvas.drawRoundRect(left, boxTop, left + size, boxTop + size, dp(3).toFloat(), dp(3).toFloat(), stroke)
      if (checked) {
        stroke.color = if ((paint.color and 0x00ffffff) > 0x7f7f7f) 0xff101010.toInt() else 0xffffffff.toInt()
        stroke.strokeWidth = dp(2).toFloat(); stroke.style = Paint.Style.STROKE
        canvas.drawLine(left + size * .22f, boxTop + size * .54f, left + size * .43f, boxTop + size * .74f, stroke)
        canvas.drawLine(left + size * .43f, boxTop + size * .74f, left + size * .80f, boxTop + size * .29f, stroke)
      }
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
      paint.color = syntaxColor(paint.color, kind)
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

  /**
   * Finds one language token for every code paragraph in a single ordered pass. A long fenced
   * block can contain thousands of paragraphs and syntax runs, so searching runs per paragraph
   * would turn formatting into quadratic work.
   */
  private fun codeLanguageLabels(ranges: List<Pair<Int, Int>>): List<String?> {
    if (ranges.isEmpty()) return emptyList()
    val result = ArrayList<String?>(ranges.size)
    var run = 0
    for ((start, end) in ranges) {
      while (run < starts.size && starts[run] + lengths.getOrElse(run) { 0 } <= start) run++
      var candidate = run
      var label: String? = null
      while (candidate < starts.size && starts[candidate] < end) {
        if (starts[candidate] + lengths.getOrElse(candidate) { 0 } > start) {
          label = languageLabel(flags[candidate])
          break
        }
        candidate++
      }
      result.add(label)
    }
    return result
  }

  companion object {
    internal data class ParagraphDecoration(val start: Int, val end: Int, val quote: Boolean, val indent: Int)
    internal data class TaskMarker(val start: Int, val checked: Boolean)
    internal data class CodeBlockSection(val start: Int, val end: Int, val label: String?, val firstParagraphEnd: Int)

    /** Paint-relative palette with a contrast branch for light and dark surfaces. */
    internal fun syntaxColor(base: Int, kind: Int): Int {
      // Dark text means a light surface; bright text means a dark surface.
      val lightSurface = ((base ushr 16 and 0xff) * 299 + (base ushr 8 and 0xff) * 587 + (base and 0xff) * 114) < 128000
      val target = if (lightSurface) when (kind) {
        TOKEN_KEYWORD -> 0xff6d28d9.toInt(); TOKEN_STRING -> 0xff15803d.toInt(); TOKEN_COMMENT -> 0xff52525b.toInt()
        TOKEN_NUMBER -> 0xffb45309.toInt(); else -> 0xff0369a1.toInt()
      } else when (kind) {
        TOKEN_KEYWORD -> 0xffa78bfa.toInt(); TOKEN_STRING -> 0xff4ade80.toInt(); TOKEN_COMMENT -> 0xffc4c4cc.toInt()
        TOKEN_NUMBER -> 0xfffbbf24.toInt(); else -> 0xff38bdf8.toInt()
      }
      fun component(shift: Int) = (((base ushr shift and 0xff) * .20f) + ((target ushr shift and 0xff) * .80f)).toInt()
      return (base and -0x1000000) or (component(16) shl 16) or (component(8) shl 8) or component(0)
    }

    const val LANGUAGE_JAVASCRIPT = 1 shl 8
    const val LANGUAGE_TYPESCRIPT = 1 shl 9
    const val LANGUAGE_JSON = 1 shl 10
    const val LANGUAGE_PYTHON = 1 shl 11
    const val LANGUAGE_MASK = 15 shl 8
    const val TASK_MARKER = 1 shl 12
    const val TASK_CHECKED = 1 shl 13

    internal fun languageLabel(flags: Int) = when (flags and LANGUAGE_MASK) {
      LANGUAGE_TYPESCRIPT -> "TYPESCRIPT"
      LANGUAGE_JSON -> "JSON"
      LANGUAGE_JAVASCRIPT -> "JAVASCRIPT"
      LANGUAGE_PYTHON -> "PYTHON"
      else -> "CODE"
    }

    /** Uses the parser's task-marker run, never task-like prose within a list paragraph. */
    internal fun taskMarkers(
        text: String, starts: IntArray, lengths: IntArray, flags: IntArray,
    ): List<TaskMarker> {
      if (flags.none { it and TASK_MARKER != 0 }) return emptyList()
      val result = ArrayList<TaskMarker>()
      for (index in starts.indices) {
        if (flags.getOrElse(index) { 0 } and TASK_MARKER == 0) continue
        val start = starts[index]
        val end = start + lengths.getOrElse(index) { 0 }
        // The native parser flags exactly its normalized four-character marker. Keep the source
        // check defensive, so malformed native data cannot conceal arbitrary content.
        if (end - start == 4 && start >= 0 && end <= text.length && text[start] == '[' &&
            (text[start + 1] == ' ' || text[start + 1] == 'x' || text[start + 1] == 'X') &&
            text[start + 2] == ']' && text[start + 3] == ' ') {
          result.add(TaskMarker(start, flags[index] and TASK_CHECKED != 0))
        }
      }
      return result
    }
    /** Keeps one label for a contiguous fenced-language section, not one label per code line. */
    internal fun codeLanguageSectionLabels(ranges: List<Pair<Int, Int>>, labels: List<String?>): List<String?> {
      val result = ArrayList<String?>(ranges.size)
      var previousEnd = -2
      var previousLabel: String? = null
      for (index in ranges.indices) {
        val label = labels.getOrNull(index)
        result.add(label?.takeIf { ranges[index].first != previousEnd + 1 || it != previousLabel })
        previousEnd = ranges[index].second
        previousLabel = label
      }
      return result
    }

    /** Coalesces same-language adjacent code paragraphs without scanning syntax-token runs again. */
    internal fun codeBlockSections(
        ranges: List<Pair<Int, Int>>, labels: List<String?>, text: String? = null,
    ): List<CodeBlockSection> {
      if (ranges.isEmpty()) return emptyList()
      val result = ArrayList<CodeBlockSection>()
      var start = ranges[0].first
      var end = ranges[0].second
      var firstParagraphEnd = end
      var label = labels.getOrNull(0)
      for (index in 1 until ranges.size) {
        val range = ranges[index]
        val nextLabel = labels.getOrNull(index)
        // Syntax tokenization is sparse: a comment-only or punctuation-only paragraph may carry
        // no language bit. Keep it on the surrounding fence surface, adopting the first known
        // label, while a different explicit label still starts a new fence section.
        // A streaming fence with no body is represented by a CODE_BLOCK run over its newline:
        // [lineStart, lineStart + 1). Its following paragraph starts at that same exclusive end,
        // whereas ordinary code paragraphs leave their newline outside the range. Both belong to
        // one surface; distinct fences retain a real separator gap.
        // Normal paragraphs exclude their newline, so the next code line starts at end + 1.
        // An empty streaming fence instead owns its newline ([start, end)); its closing fence's
        // plain separator is the following character and must keep the next fence separate.
        val previousOwnsNewline = text != null && end > start && text[end - 1] == '\n'
        if ((range.first == end || (range.first == end + 1 && !previousOwnsNewline)) &&
            (label == null || nextLabel == null || nextLabel == label)) {
          end = range.second
          if (label == null) label = nextLabel
        } else {
          result.add(CodeBlockSection(start, end, label, firstParagraphEnd))
          start = range.first; end = range.second; firstParagraphEnd = end; label = nextLabel
        }
      }
      result.add(CodeBlockSection(start, end, label, firstParagraphEnd))
      return result
    }

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
    const val BLOCK_IMAGE_PENDING = 3

    /**
     * Returns each code-block paragraph once. This scans text and ordered parser runs forward,
     * so a run spanning multiple paragraphs cannot suppress a later paragraph's margin.
     */
    internal fun codeBlockMarginRanges(
        text: String, starts: IntArray, lengths: IntArray, flags: IntArray,
    ): List<Pair<Int, Int>> {
      if (flags.none { it and CODE_BLOCK != 0 }) return emptyList()
      val result = ArrayList<Pair<Int, Int>>()
      var run = 0
      var lineStart = 0
      while (lineStart < text.length) {
        var lineEnd = text.indexOf('\n', lineStart)
        if (lineEnd == -1) lineEnd = text.length
        while (run < starts.size && starts[run] + lengths.getOrElse(run) { 0 } <= lineStart) run++
        var candidate = run
        var hasCode = false
        // A just-opened fence has one synthetic CODE_BLOCK run over its newline. Include that
        // zero-glyph paragraph so LineBackgroundSpan receives a drawable visual line.
        while (candidate < starts.size && (starts[candidate] < lineEnd ||
            (lineStart == lineEnd && starts[candidate] == lineEnd))) {
          val runEnd = starts[candidate] + lengths.getOrElse(candidate) { 0 }
          if (runEnd > lineStart && flags.getOrElse(candidate) { 0 } and CODE_BLOCK != 0) {
            hasCode = true
            break
          }
          candidate++
        }
        val paragraphEnd = if (lineStart == lineEnd && hasCode) {
          (lineEnd + 1).coerceAtMost(text.length)
        } else lineEnd
        if (hasCode && lineStart < paragraphEnd) result.add(lineStart to paragraphEnd)
        lineStart = lineEnd + 1
      }
      return result
    }

    /**
     * Rewinds an offset in a contiguous fenced-code cluster to that cluster's first run.
     * Parser runs are ordered and syntax highlighting only splits a code run into adjacent
     * pieces, so this is one allocation-free pass. It deliberately does not scan every source
     * paragraph on the UI thread while a stream is updating.
     */
    internal fun codeBlockClusterStart(
        text: String, starts: IntArray, lengths: IntArray, flags: IntArray, offset: Int,
    ): Int {
      val at = offset.coerceIn(0, text.length)
      var clusterStart = -1
      var clusterEnd = -1
      for (index in starts.indices) {
        if (flags.getOrElse(index) { 0 } and CODE_BLOCK == 0) continue
        val start = starts[index].coerceIn(0, text.length)
        val end = (start + lengths.getOrElse(index) { 0 }).coerceIn(start, text.length)
        if (clusterStart < 0 || start > clusterEnd) {
          // Include the exclusive end: streaming a just-closed fence can put the paragraph
          // boundary there while the former code span still owns that boundary.
          if (at in clusterStart..clusterEnd) return clusterStart
          if (at < start) return at
          clusterStart = start
          clusterEnd = end
        } else if (end > clusterEnd) {
          clusterEnd = end
        }
      }
      return if (at in clusterStart..clusterEnd) clusterStart else at
    }

    /** One quote/list decoration per paragraph, independent of how the parser tokenizes it. */
    internal fun paragraphDecorations(
        text: String, starts: IntArray, lengths: IntArray, flags: IntArray, indents: IntArray,
    ): List<ParagraphDecoration> {
      if (flags.none { it and QUOTE != 0 } && indents.none { it > 0 }) return emptyList()
      val result = ArrayList<ParagraphDecoration>()
      var run = 0
      var lineStart = 0
      while (lineStart < text.length) {
        var lineEnd = text.indexOf('\n', lineStart)
        if (lineEnd == -1) lineEnd = text.length
        while (run < starts.size && starts[run] + lengths.getOrElse(run) { 0 } <= lineStart) run++
        var candidate = run
        var quote = false
        var indent = 0
        while (candidate < starts.size && starts[candidate] < lineEnd) {
          if (starts[candidate] + lengths.getOrElse(candidate) { 0 } > lineStart) {
            quote = quote || flags.getOrElse(candidate) { 0 } and QUOTE != 0
            indent = maxOf(indent, indents.getOrElse(candidate) { 0 })
          }
          candidate++
        }
        if (quote || indent > 0) result.add(ParagraphDecoration(lineStart, lineEnd, quote, indent))
        lineStart = lineEnd + 1
      }
      return result
    }

    private fun dp(value: Int) = (value * android.content.res.Resources.getSystem().displayMetrics.density + 0.5f).toInt()
    private fun isSafeUrl(url: String): Boolean {
      val scheme = url.substringBefore(':', "").lowercase()
      return scheme == "http" || scheme == "https" || scheme == "mailto"
    }
  }
}
