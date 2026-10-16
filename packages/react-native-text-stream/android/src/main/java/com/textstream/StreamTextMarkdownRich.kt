package com.textstream

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Rect
import com.facebook.react.uimanager.PixelUtil
import android.text.StaticLayout
import android.text.TextPaint
import android.text.style.ReplacementSpan
import kotlin.math.max

/** Immutable, worker-built table. ReplacementSpan keeps a table atomic for DynamicLayout reflow. */
internal class MarkdownTableSpan(
    private val block: StreamTextMarkdown.RichBlock,
    val width: Int,
    style: StreamTextStyle,
) : ReplacementSpan() {
  private val columns = block.columnCount.coerceAtLeast(1)
  private val columnWidth = width.toFloat() / columns
  private val gap = minOf(1f, columnWidth / 8)
  private val pad = minOf(8f, columnWidth / 8)
  private val cellWidth = max(1, (columnWidth - gap - pad * 2).toInt())
  private val clip = Rect()
  private val paint = style.paint(StreamTextPreparedLayout.fontAssets)
  private val fillPaint = Paint(paint).apply { color = (paint.color and 0x00ffffff) or 0x18000000 }
  private val strokePaint = Paint(paint).apply { this.style = Paint.Style.STROKE; strokeWidth = 1f; color = (paint.color and 0x00ffffff) or 0x55000000 }
  private val layouts: Array<StaticLayout> = Array((block.cells ?: emptyArray()).size) { i ->
    val cell = block.cells!![i]
    val cellDocument = StreamTextMarkdown(cell.text, cell.starts, cell.lengths, cell.flags, cell.headings, cell.indents, cell.urls)
    val styled = cellDocument.styled(paint.color)
    val aligned = style.copy(textAlign = when (cell.alignment) { 2 -> "center"; 3 -> "right"; else -> "left" })
    val cellPaint = TextPaint(paint).apply { if (cell.header) typeface = android.graphics.Typeface.create(typeface, android.graphics.Typeface.BOLD) }
    aligned.staticLayout(styled, cellPaint, cellWidth)
  }
  private val rows = IntArray(block.rowCount) { row ->
    var h = 0
    for (column in 0 until columns) layouts.getOrNull(row * columns + column)?.let { h = max(h, it.height) }
    kotlin.math.ceil(h + pad * 2 + gap).toInt()
  }
  private val rowOffsets = IntArray(rows.size + 1).also { offsets -> for (i in rows.indices) offsets[i + 1] = offsets[i] + rows[i] }
  val height: Int = rows.sum() + kotlin.math.ceil(gap).toInt()

  override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
    fm?.let { it.ascent = -height; it.top = -height; it.descent = 0; it.bottom = 0 }
    return width
  }

  override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float, top: Int, y: Int, bottom: Int, paint: Paint) {
    fillPaint.color = (paint.color and 0x00ffffff) or 0x18000000
    strokePaint.color = (paint.color and 0x00ffffff) or 0x55000000
    if (rows.isEmpty() || !canvas.getClipBounds(clip)) return
    val origin = y - height
    var row = rowOffsets.binarySearch((clip.top - origin).coerceAtLeast(0)).let { if (it < 0) (-it - 2).coerceAtLeast(0) else it.coerceAtMost(rows.lastIndex) }
    var rowTop = origin + gap + rowOffsets[row]
    while (row < rows.size && rowTop < clip.bottom) {
      var left = x + gap
      for (column in 0 until columns) {
        val index = row * columns + column; val layout = layouts.getOrNull(index) ?: continue
        val cell = block.cells!![index]
        val right = minOf(x + width, left + columnWidth - gap); val bottom = (rowTop + rows[row] - gap).toFloat()
        if (cell.header) canvas.drawRect(left, rowTop.toFloat(), right, bottom, fillPaint)
        canvas.drawRect(left, rowTop.toFloat(), right, bottom, strokePaint)
        layout.paint.color = paint.color
        canvas.save(); canvas.clipRect(left, rowTop, right, bottom)
        canvas.translate(left + pad, rowTop + pad)
        layout.draw(canvas); canvas.restore()
        left += columnWidth
      }
      rowTop += rows[row]
      row++
    }
  }
}

/**
 * A stable image attachment. A null [url] is an intentionally incomplete streaming image: it
 * paints the same low-cost placeholder as a loading image, but never enters the loader or
 * allocates a request subscription. The next snapshot replaces it at the same U+FFFC position
 * once the parser supplies the complete URL.
 */
internal class MarkdownImageSpan(private val url: String?, private val alt: String, private val maxWidth: Int) : ReplacementSpan() {
  @Volatile var status = if (url == null) "pending" else "placeholder"; private set
  @Volatile private var bitmap: android.graphics.Bitmap? = null
  private var requested = false
  private var onChanged: (() -> Unit)? = null
  private var cancel: (() -> Unit)? = null
  val pixelWidth get() = bitmap?.width ?: 0
  val pixelHeight get() = bitmap?.height ?: 0
  val width get() = bitmap?.let { minOf(maxWidth.coerceAtLeast(1), PixelUtil.toPixelFromDIP(it.width.toFloat()).toInt()) } ?: placeholderWidth
  val height get() = bitmap?.let { (width.toFloat() * it.height / it.width).toInt().coerceAtLeast(1) } ?: placeholderHeight
  private val placeholderWidth get() = minOf(maxWidth.coerceAtLeast(1), (maxWidth * 0.45f).toInt().coerceAtLeast(48))
  private val placeholderHeight get() = (placeholderWidth * 0.56f).toInt().coerceAtLeast(1)
  private val bounds = RectF()
  private val fill = Paint()
  private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG)
  // The pending and loading states deliberately use one short fixed label. Growing alt text is
  // transport metadata, not visual content: rendering it here would reflow or clip each chunk.
  private val pendingLabel = "Image"
  private val errorLabel = if (alt.isEmpty()) "Image" else "[$alt]"
  fun bind(onChanged: () -> Unit) {
    this.onChanged = onChanged
    // Do not turn a partial Markdown URL into an HTTP request. The parser only provides a URL
    // for a fully closed image destination, so pending -> loading is an explicit state change.
    val source = url ?: return
    if (requested) return; requested = true; status = "loading"
    cancel = StreamTextMarkdownImages.load(source) { result ->
      when (result) { is StreamTextMarkdownImages.Result.Loaded -> { bitmap = result.bitmap; status = "loaded" }
        StreamTextMarkdownImages.Result.Blocked -> status = "blocked"; StreamTextMarkdownImages.Result.Oversize -> status = "oversize"; else -> status = "error" }
      this.onChanged?.invoke()
    }
  }
  fun cancel() { cancel?.invoke(); cancel = null; onChanged = null }
  fun matches(other: MarkdownImageSpan) = url == other.url && alt == other.alt && maxWidth == other.maxWidth
  override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
    val h = height; fm?.let { it.ascent = -h; it.top = -h; it.descent = 0; it.bottom = 0 }; return width
  }
  override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float, top: Int, y: Int, bottom: Int, paint: Paint) {
    val topY = y - height
    bounds.set(x, topY.toFloat(), x + width, y.toFloat())
    bitmap?.let { canvas.drawBitmap(it, null, bounds, paint); return }
    fill.color = (paint.color and 0x00ffffff) or 0x18000000
    canvas.drawRect(bounds, fill)
    canvas.save(); canvas.clipRect(bounds)
    labelPaint.set(paint)
    labelPaint.textSize = PixelUtil.toPixelFromDIP(12f)
    // Keep the compact fixed label for the streaming states. Alt remains in accessibility, and
    // is intentionally shown only for a terminal image error as useful fallback context.
    val visibleLabel = if (status == "error" || status == "blocked" || status == "oversize") errorLabel else pendingLabel
    val inset = PixelUtil.toPixelFromDIP(4f)
    canvas.drawText(visibleLabel, x + inset, topY + inset + labelPaint.textSize, labelPaint)
    canvas.restore()
  }
}
