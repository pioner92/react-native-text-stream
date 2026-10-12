package com.textstream

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RenderNode
import android.graphics.Typeface
import android.os.Build
import android.text.DynamicLayout
import android.text.Layout
import android.text.SpannableStringBuilder
import android.text.TextDirectionHeuristics
import android.text.TextPaint
import android.util.Log
import android.util.TypedValue
import android.view.Choreographer
import android.view.accessibility.AccessibilityNodeInfo
import com.facebook.react.bridge.ReactContext
import com.facebook.react.uimanager.PixelUtil
import com.facebook.react.uimanager.StateWrapper
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.views.text.ReactTypefaceUtils
import com.facebook.react.views.view.ReactViewGroup
import java.nio.ByteBuffer
import kotlin.math.max
import kotlin.math.min

/**
 * Renders a TSB1 buffer as it fills and sizes itself to the text: after each append it reports
 * the laid-out height (only when it changes, i.e. per line) to the shadow node's state, on the
 * main thread. No JS is involved per chunk. The Android counterpart of ios/fabric/StreamTextRenderer.
 *
 * Cost model:
 * - a background watcher reads the header every few milliseconds and wakes the main thread as soon
 *   as bytes or a final status land; without new bytes the main thread sleeps;
 * - a wake appends at most `bytesPerTick`, adapted so the work stays within [FRAME_BUDGET_NS]; a
 *   burst is spread over frames by the Choreographer, which runs only while there is a backlog;
 * - a DynamicLayout re-wraps only the paragraph an append touched, and the text is drawn in blocks
 *   of lines, each its own display list: an append re-records only the last block.
 */
@SuppressLint("ViewConstructor")
class StreamTextView(context: Context) : ReactViewGroup(context) {
  // Style, set by the view manager; applied in [applyStyle].
  var fontSize = 15f
  var fontFamily: String? = null
  var fontWeight: String? = null
  var textColor: Int? = null
  var lineHeight = 0f
  var letterSpacing = 0f
  var textAlign: String? = null

  private val paint = TextPaint(Paint.ANTI_ALIAS_FLAG)
  private val text = SpannableStringBuilder()
  private var textLayout: DynamicLayout? = null
  private var layoutWidth = -1
  /** With a `lineHeight`: the space TextKit puts above each line, which the layout puts below. */
  private var lineTopExtra = 0
  private var appliedStyle: List<Any?>? = null

  // Reader state, main thread only.
  private var source: StreamTextSource? = null
  private var base = 0L
  private var bytes: ByteBuffer? = null
  private var capacity = 0L
  private var offset = 0L
  private var finished = false
  private var bytesPerTick = 32 * 1024
  private val decoder = Utf8StreamDecoder()
  private val decoded = StringBuilder()
  private val chunk = ByteArray(SLICE_BYTES)
  private val watcher = StreamTextWatcher { watcherWoke() }
  private var frameScheduled = false
  private val frameCallback = Choreographer.FrameCallback {
    frameScheduled = false
    tick()
  }

  // Fabric.
  private var stateWrapper: StateWrapper? = null
  private var propSourceAddress = 0L
  private var propGeneration = 0
  /** The JS wrapper's generation of the attached source, sent back with each status event. */
  private var generation = 0
  private var revision = 0
  /** The text height in dp, as last committed. Starts at the shadow node state's initial height. */
  private var reportedHeight = 0f
  private var heightPending = false
  private var reportedStatus: String? = null

  // Blocks of lines, each recorded once into its own display list (API 29+).
  private val blocks = ArrayList<RenderNode>()
  /** Blocks [0, cleanBlocks) are recorded and current. */
  private var cleanBlocks = 0

  /** False while ReactViewGroup's constructor runs, before the fields above are set. */
  private var initialized = false

  init {
    setWillNotDraw(false)
    initialized = true
  }

  // MARK: - Fabric

  /** A new state: picks up a new `source` prop (Android props can't carry the buffer). */
  fun setState(wrapper: StateWrapper) {
    stateWrapper = wrapper
    val propSource = StreamTextNative.stateSource(wrapper)
    val address = propSource?.address() ?: 0L
    val propGen = StreamTextNative.stateGeneration(wrapper)
    // Compared with the previous prop, not with what is attached: an `attach` command may have
    // replaced the buffer since, and an unrelated update must not undo it. No source before and
    // after is no change, whatever the generation (a view mounted without one stays quiet).
    val changed = address != propSourceAddress || (address != 0L && propGen != propGeneration)
    propSourceAddress = address
    propGeneration = propGen
    if (changed) attach(propSource, propGen) else propSource?.release()
    if (heightPending) commitHeight()
  }

  /** The `attach` command: a buffer put in StreamTextBuffers. */
  fun attachCommand(bufferId: Int, generation: Int) {
    attach(if (bufferId > 0) StreamTextNative.takeBuffer(bufferId) else null, generation)
  }

  fun clearCommand(generation: Int) {
    attach(null, generation)
  }

  /** The view is gone: stop reading the buffer and let go of it. */
  fun dispose() {
    stopTicking()
    watcher.stop()
    releaseSource()
    discardBlocks()
  }

  // MARK: - Attaching

  /** Starts showing `newSource` from its first byte; null clears the view. Takes ownership of it. */
  private fun attach(newSource: StreamTextSource?, generation: Int) {
    stopTicking()
    watcher.stop()
    // A new source starts its own status sequence (a finished one reports only its final status).
    reportedStatus = null
    this.generation = generation
    releaseSource()
    offset = 0
    finished = false
    decoder.reset()
    if (text.isNotEmpty()) {
      text.clear()
      markDirty(0)
    }
    try {
      val size = newSource?.size() ?: 0L
      if (newSource == null || size < StreamTextNative.HEADER_SIZE) {
        newSource?.release()
        reportStatus("idle", 0)
        return
      }
      val address = newSource.address()
      if (!StreamTextNative.isValid(address, size)) {
        Log.w(TAG, "source is not a valid TSB1 buffer")
        newSource.release()
        reportStatus("idle", 0)
        return
      }
      source = newSource
      base = address
      bytes = newSource.bytes()
      capacity = min(StreamTextNative.capacity(address), Int.MAX_VALUE.toLong() - StreamTextNative.HEADER_SIZE)
      // A finished stream (e.g. a message remounted by a list) that is small enough is shown in
      // one go, at its final height, instead of being typed out again over several frames.
      val instantFill = StreamTextNative.status(address) != StreamTextNative.STATUS_OPEN &&
          StreamTextNative.committed(address) <= INSTANT_FILL_BYTES
      if (!instantFill) {
        reportStatus("streaming", 0)
        if (isAttachedToWindow) watcher.start(address, 0)
      }
      tick(fillAll = instantFill)
    } finally {
      // The height is reported once at the end: clearing and refilling in one go is one commit.
      reportHeight()
    }
  }

  private fun releaseSource() {
    base = 0
    bytes = null
    capacity = 0
    source?.release()
    source = null
  }

  override fun onAttachedToWindow() {
    super.onAttachedToWindow()
    if (base != 0L && !finished) {
      watcher.start(base, offset)
      scheduleFrame()
    }
  }

  override fun onDetachedFromWindow() {
    super.onDetachedFromWindow()
    stopTicking()
    watcher.stop()
    discardBlocks()
  }

  // MARK: - Frame loop

  private fun scheduleFrame() {
    if (frameScheduled) return
    frameScheduled = true
    Choreographer.getInstance().postFrameCallback(frameCallback)
  }

  private fun stopTicking() {
    if (!frameScheduled) return
    frameScheduled = false
    Choreographer.getInstance().removeFrameCallback(frameCallback)
  }

  /** New bytes or a final status landed: show them now, not at the next frame. */
  private fun watcherWoke() {
    watcher.didWake()
    if (base == 0L || finished) return
    tick()
  }

  private fun tick(fillAll: Boolean = false) {
    val base = base
    val bytes = bytes
    if (base == 0L || bytes == null || finished) {
      stopTicking()
      return
    }
    // Status first: once it is terminal, the committed value read after it is final.
    val status = StreamTextNative.status(base)
    val committed = min(StreamTextNative.committed(base), capacity)

    if (committed > offset) {
      val budget = if (fillAll) committed - offset else bytesPerTick.toLong()
      val end = min(committed, offset + budget)
      val started = System.nanoTime()
      val appended = end - offset
      decoded.setLength(0)
      while (offset < end) {
        val count = min(end - offset, SLICE_BYTES.toLong()).toInt()
        bytes.position(StreamTextNative.HEADER_SIZE + offset.toInt())
        bytes.get(chunk, 0, count)
        decoder.decode(chunk, 0, count, decoded)
        offset += count
      }
      appendText(decoded)
      // Before the first layout there is no width: the text is laid out once the view gets one.
      val measured = textLayout != null
      val commitTime = if (fillAll) 0L else reportHeight()
      val tickTime = System.nanoTime() - started
      // Re-estimate from ticks that hit the budget and were laid out (small appends say little
      // about the cost per byte), without the commit, whose cost doesn't grow with the bytes. The
      // budget at most doubles per tick, so one cheap tick can't schedule a huge append.
      if (!fillAll && measured && appended >= budget) {
        val perByte = max(tickTime - commitTime, 1_000L).toDouble() / appended
        val estimate = min((FRAME_BUDGET_NS / perByte).toLong(), bytesPerTick * 2L).toInt()
        bytesPerTick = estimate.coerceIn(MIN_BYTES_PER_TICK, MAX_BYTES_PER_TICK)
      }
    }

    if (status != StreamTextNative.STATUS_OPEN && offset >= committed) {
      // A sequence cut short at the very end: show it as U+FFFD rather than dropping it.
      decoded.setLength(0)
      decoder.finish(decoded)
      if (decoded.isNotEmpty()) appendText(decoded)
      if (!fillAll) reportHeight()
      finished = true
      stopTicking()
      // Everything is in the text now: let go of the buffer (the watcher first, as it reads it).
      watcher.stop()
      releaseSource()
      reportStatus(terminalStatus(status), committed)
      return
    }
    // Frames only while there is a backlog; otherwise the watcher wakes us.
    if (offset < committed && isAttachedToWindow) scheduleFrame() else stopTicking()
  }

  private fun terminalStatus(status: Int): String =
      when (status) {
        StreamTextNative.STATUS_DONE -> "done"
        StreamTextNative.STATUS_CANCELLED -> "cancelled"
        StreamTextNative.STATUS_OVERFLOW -> "truncated"
        else -> "error"
      }

  private fun appendText(chars: CharSequence) {
    if (chars.isEmpty()) return
    val oldLength = text.length
    text.append(chars)
    markDirty(oldLength)
  }

  // MARK: - Measuring

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    updateLayoutWidth()
  }

  override fun setPadding(left: Int, top: Int, right: Int, bottom: Int) {
    super.setPadding(left, top, right, bottom)
    updateLayoutWidth()
  }

  /** Only a new width re-wraps the text; a new height is our own report coming back. */
  private fun updateLayoutWidth() {
    if (!initialized) return
    val width = width - paddingLeft - paddingRight
    if (width == layoutWidth) return
    layoutWidth = width
    rebuildLayout()
    reportHeight()
  }

  /** Returns the time (ns) spent committing the new height, if there was one. */
  private fun reportHeight(): Long {
    val layout = textLayout ?: return 0
    val started = System.nanoTime()
    // Like <Text>, an empty view has no height (and mounting one doesn't need a commit).
    val px = if (text.isEmpty()) 0 else layout.height + lineTopExtra
    val height = PixelUtil.toDIPFromPixel(px.toFloat())
    if (height == reportedHeight) return 0
    reportedHeight = height
    commitHeight()
    return System.nanoTime() - started
  }

  private fun commitHeight() {
    val wrapper = stateWrapper
    if (wrapper == null) {
      heightPending = true
      return
    }
    heightPending = false
    revision++
    StreamTextNative.updateHeight(wrapper, reportedHeight, revision)
  }

  private fun reportStatus(status: String, bytes: Long) {
    if (status == reportedStatus) return
    reportedStatus = status
    val reactContext = context as? ReactContext ?: return
    val dispatcher = UIManagerHelper.getEventDispatcherForReactTag(reactContext, id) ?: return
    dispatcher.dispatchEvent(
        StatusChangeEvent(UIManagerHelper.getSurfaceId(this), id, status, bytes.toDouble(), generation))
  }

  // MARK: - Style

  /** Re-applies the style props; re-wraps the text only when one that moves the lines changed. */
  fun applyStyle() {
    val style = listOf(fontSize, fontFamily, fontWeight, lineHeight, letterSpacing, textAlign)
    val color = textColor ?: defaultTextColor()
    if (paint.color != color) {
      paint.color = color
      markDirty(0)
    }
    if (style == appliedStyle) return
    appliedStyle = style
    paint.textSize = PixelUtil.toPixelFromDIP(fontSize)
    paint.letterSpacing = if (fontSize > 0) letterSpacing / fontSize else 0f
    paint.typeface = ReactTypefaceUtils.applyStyles(
        null, Typeface.NORMAL, ReactTypefaceUtils.parseFontWeight(fontWeight), fontFamily, context.assets)
    rebuildLayout()
    reportHeight()
  }

  private fun defaultTextColor(): Int {
    val value = TypedValue()
    if (!context.theme.resolveAttribute(android.R.attr.textColorPrimary, value, true)) return Color.BLACK
    return if (value.resourceId != 0) context.getColor(value.resourceId) else value.data
  }

  private fun rebuildLayout() {
    markDirty(0)
    if (layoutWidth <= 0) {
      textLayout = null
      return
    }
    val alignment = when (textAlign) {
      "right" -> Layout.Alignment.ALIGN_OPPOSITE
      "center" -> Layout.Alignment.ALIGN_CENTER
      else -> Layout.Alignment.ALIGN_NORMAL
    }
    // TextKit puts a line's extra height above it; the layout adds `spacingAdd` below. The view
    // draws `lineTopExtra` lower, so the baselines match.
    val metrics = paint.fontMetricsInt
    val natural = metrics.descent - metrics.ascent
    val spacingAdd = if (lineHeight > 0) Math.round(PixelUtil.toPixelFromDIP(lineHeight)) - natural else 0
    lineTopExtra = spacingAdd
    textLayout = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      DynamicLayout.Builder.obtain(text, paint, layoutWidth)
          .setAlignment(alignment)
          .setLineSpacing(spacingAdd.toFloat(), 1f)
          .setIncludePad(false)
          .setTextDirection(TextDirectionHeuristics.FIRSTSTRONG_LTR)
          // Greedy wrapping, as TextKit: an append never re-wraps the lines above it.
          .setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE)
          .setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE)
          .setJustificationMode(
              if (textAlign == "justify") Layout.JUSTIFICATION_MODE_INTER_WORD else Layout.JUSTIFICATION_MODE_NONE)
          .build()
    } else {
      @Suppress("DEPRECATION")
      DynamicLayout(text, paint, layoutWidth, alignment, 1f, spacingAdd.toFloat(), false)
    }
  }

  // MARK: - Drawing

  /** The text from `charOffset` on changed: re-record the blocks it touches. */
  private fun markDirty(charOffset: Int) {
    val layout = textLayout
    val block = if (layout == null || charOffset == 0) 0 else {
      // The line the append continued may re-wrap (one more for safety).
      max(layout.getLineForOffset(charOffset) - 1, 0) / BLOCK_LINES
    }
    cleanBlocks = min(cleanBlocks, block)
    invalidate()
  }

  private fun discardBlocks() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      for (node in blocks) node.discardDisplayList()
    }
    blocks.clear()
    cleanBlocks = 0
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)
    val layout = textLayout ?: return
    if (text.isEmpty()) return
    canvas.save()
    canvas.translate(paddingLeft.toFloat(), (paddingTop + lineTopExtra).toFloat())
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && canvas.isHardwareAccelerated) {
      drawBlocks(canvas, layout)
    } else {
      layout.draw(canvas)
    }
    canvas.restore()
  }

  @androidx.annotation.RequiresApi(Build.VERSION_CODES.Q)
  private fun drawBlocks(canvas: Canvas, layout: Layout) {
    val lineCount = layout.lineCount
    val count = (lineCount + BLOCK_LINES - 1) / BLOCK_LINES
    while (blocks.size > count) blocks.removeAt(blocks.size - 1).discardDisplayList()
    while (blocks.size < count) blocks.add(RenderNode("StreamTextBlock"))
    for (i in min(cleanBlocks, count) until count) {
      val first = i * BLOCK_LINES
      val last = min(first + BLOCK_LINES, lineCount)
      val top = layout.getLineTop(first)
      val bottom = layout.getLineTop(last)
      val node = blocks[i]
      node.setPosition(0, top, layoutWidth, bottom)
      val recording = node.beginRecording()
      try {
        // Layout.draw picks the lines in the clip, which is the block's bounds.
        recording.translate(0f, -top.toFloat())
        layout.draw(recording)
      } finally {
        node.endRecording()
      }
    }
    cleanBlocks = count
    for (node in blocks) canvas.drawRenderNode(node)
  }

  // MARK: - Accessibility

  /** Only the end of the text: a snapshot of megabytes of it would stall the main thread. */
  override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
    super.onInitializeAccessibilityNodeInfo(info)
    val length = text.length
    info.text = if (length <= ACCESSIBLE_TAIL_CHARS) text.toString() else "…" + text.subSequence(length - ACCESSIBLE_TAIL_CHARS, length)
  }

  private companion object {
    const val TAG = "StreamText"
    const val SLICE_BYTES = 16 * 1024
    /** Main-thread time per frame we allow for append + layout (a 60 Hz frame is ~16.7 ms). */
    const val FRAME_BUDGET_NS = 6_000_000.0
    const val MIN_BYTES_PER_TICK = 4 * 1024
    const val MAX_BYTES_PER_TICK = 256 * 1024
    const val INSTANT_FILL_BYTES = 64L * 1024
    const val BLOCK_LINES = 32
    const val ACCESSIBLE_TAIL_CHARS = 2000
  }
}
