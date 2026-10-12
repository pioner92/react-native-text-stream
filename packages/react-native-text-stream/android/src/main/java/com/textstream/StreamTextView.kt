package com.textstream

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RenderNode
import android.os.Build
import android.text.DynamicLayout
import android.text.Layout
import android.text.SpannableStringBuilder
import android.text.TextPaint
import android.util.Log
import android.util.TypedValue
import android.view.Choreographer
import android.view.accessibility.AccessibilityNodeInfo
import com.facebook.react.bridge.ReactContext
import com.facebook.react.uimanager.PixelUtil
import com.facebook.react.uimanager.StateWrapper
import com.facebook.react.uimanager.UIManagerHelper
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
 * - a finished `source` of up to 64 KB comes with a layout the shadow node prepared off the main
 *   thread (StreamTextPreparedLayout): it is drawn as is, a "canvas", and nothing is laid out here;
 * - otherwise a background watcher reads the header every few milliseconds and wakes the main
 *   thread as soon as bytes or a final status land; without new bytes the main thread sleeps;
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
  var selectable = true

  /** Tests and benchmarks (STREAMTEXT_TESTING): `nativeID` with `recordDisplayLog`. */
  var recordDisplayLog = false
  private var displayLogKey: String? = null
  /** Bytes appended and not yet drawn: logged when they are. */
  private var unloggedShown = -1L
  /** Text was laid out here at least once (the iOS text view exists). */
  private var hadText = false

  private var style = StreamTextStyle()
  private var styleApplied = false
  private var paint = TextPaint(Paint.ANTI_ALIAS_FLAG)
  /** The text the view laid out itself: streamed, attached, or a canvas it fell back from. */
  private val text = SpannableStringBuilder()
  private var textLayout: DynamicLayout? = null
  /** Content width in px. */
  private var layoutWidth = -1

  /** A prepared layout shown as is; then [text] stays empty. */
  private var canvas: StreamTextPreparedLayout? = null
  /** The layout the latest state prepared for the `source` prop (a new width or style). */
  private var latestPrepared: StreamTextPreparedLayout? = null
  /** The canvas may not fit the width or the style any more: settled once the update is applied. */
  private var canvasStale = false
  private val settleCanvas = Runnable { if (canvasStale) syncCanvas(fallBack = true) }

  // Reader state, main thread only.
  private var source: StreamTextSource? = null
  /** The source came from the `source` prop (not an `attach` command). */
  private var sourceIsProp = false
  /** A finished prop source shown in [text], for a background layout when the view goes. */
  private var finishedProp: StreamTextSource? = null
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
    val prepared = StreamTextNative.statePrepared(wrapper)
    // Compared with the previous prop, not with what is attached: an `attach` command may have
    // replaced the buffer since, and an unrelated update must not undo it. No source before and
    // after is no change, whatever the generation (a view mounted without one stays quiet).
    val changed = address != propSourceAddress || (address != 0L && propGen != propGeneration)
    propSourceAddress = address
    propGeneration = propGen
    latestPrepared = prepared
    if (changed) {
      attach(propSource, propGen, prepared, fromProp = true)
    } else {
      propSource?.release()
      // Same text, maybe a new width or style.
      if (canvas != null) syncCanvas(fallBack = false)
    }
    if (heightPending) commitHeight()
  }

  /** The `attach` command: a buffer put in StreamTextBuffers. */
  fun attachCommand(bufferId: Int, generation: Int) {
    attach(if (bufferId > 0) StreamTextNative.takeBuffer(bufferId) else null, generation, null)
  }

  fun clearCommand(generation: Int) {
    attach(null, generation, null)
  }

  /** The view is gone: stop reading the buffer and let go of it. */
  fun dispose() {
    stopTicking()
    watcher.stop()
    // A finished `source` prop it laid out itself (it streamed it): lay it out in the background
    // for the next mount (texts that stay on screen never pay for it).
    finishedProp?.let {
      if (canvas == null && layoutWidth > 0) {
        StreamTextNative.prepareInBackground(
            it, PixelUtil.toDIPFromPixel(layoutWidth.toFloat()), style.fontSize, style.fontFamily, style.fontWeight,
            style.lineHeight, style.letterSpacing, style.textAlign)
      }
    }
    releaseFinishedProp()
    releaseSource()
    discardBlocks()
    canvas = null
    latestPrepared = null
    setDisplayLogKey(null)
  }

  /** Tests and benchmarks: records when bytes reach the screen under `key` (null stops). */
  fun setDisplayLogKey(key: String?) {
    if (!StreamTextDebug.enabled || key == displayLogKey) return
    displayLogKey?.let { StreamTextDebug.unregister(it, this) }
    displayLogKey = key
    key?.let { StreamTextDebug.register(it, this) }
  }

  /** What the view shows, for tests. Main thread. */
  fun debugState(): StreamTextDebugState {
    val c = paint.color
    return StreamTextDebugState(
        text = shownText().toString(),
        height = reportedHeight,
        status = reportedStatus ?: "",
        hasTextView = hadText,
        fontSize = fontSize,
        color = "#%02X%02X%02X%02X".format(Color.red(c), Color.green(c), Color.blue(c), Color.alpha(c)),
        selectable = selectable,
        accessibilityValue = accessibilityText(),
        mode = if (canvas != null) "canvas" else if (hadText) "textView" else "",
        engine = if (canvas != null) "staticLayout" else "",
        tiles = blocks.size,
    )
  }

  private fun shownText(): CharSequence = canvas?.text ?: text

  // MARK: - Attaching

  /**
   * Starts showing `newSource` from its first byte; null clears the view. Takes ownership of it.
   * `prepared`: the shadow node's layout of a finished `source` prop; an `attach` command has none.
   */
  private fun attach(
      newSource: StreamTextSource?,
      generation: Int,
      prepared: StreamTextPreparedLayout?,
      fromProp: Boolean = false,
  ) {
    stopTicking()
    watcher.stop()
    releaseFinishedProp()
    sourceIsProp = fromProp
    // A new source starts its own status sequence (a finished one reports only its final status).
    reportedStatus = null
    this.generation = generation
    releaseSource()
    offset = 0
    finished = false
    decoder.reset()
    unloggedShown = -1
    displayLogKey?.let { StreamTextDebug.clearLog(it) }
    canvasStale = false
    if (canvas != null) {
      canvas = null
      markDirty(0)
    }
    if (text.isNotEmpty()) {
      text.clear()
      style.applyLineHeight(text)
      markDirty(0)
    }
    var fromState = false
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
      val status = StreamTextNative.status(address)
      val committed = min(StreamTextNative.committed(address), capacity)
      val instantFill = status != StreamTextNative.STATUS_OPEN && committed <= INSTANT_FILL_BYTES
      // A freshly mounted view has no width yet: the layout is for the width Yoga gave it.
      if (instantFill && prepared != null && prepared.bytes == committed &&
          (layoutWidth <= 0 || prepared.fits(layoutWidth, style))) {
        showPrepared(prepared, status, committed)
        fromState = true
        return
      }
      if (!instantFill) {
        reportStatus("streaming", 0)
        if (isAttachedToWindow) watcher.start(address, 0)
      }
      tick(fillAll = instantFill)
    } finally {
      // The height is reported once at the end: clearing and refilling in one go is one commit.
      if (!fromState) reportHeight()
    }
  }

  /** Draws the prepared layout as is: no decoding, no layout here. Its height is the state's. */
  private fun showPrepared(prepared: StreamTextPreparedLayout, status: Int, committed: Long) {
    canvas = prepared
    reportedHeight = prepared.height
    finished = true
    releaseSource()
    markDirty(0)
    if (displayLogKey != null) unloggedShown = committed
    reportStatus(terminalStatus(status), committed)
  }

  /**
   * The canvas against the current width and style: the latest prepared layout if it fits, else
   * the current one if it still does, else (`fallBack`) the text laid out here, as iOS swaps in a
   * text view. Without `fallBack` the decision waits for the rest of the update.
   */
  private fun syncCanvas(fallBack: Boolean) {
    val current = canvas ?: return
    val width = layoutWidth
    if (width <= 0) return
    val candidate = latestPrepared
    if (candidate != null && candidate !== current && candidate.text == current.text && candidate.fits(width, style)) {
      canvas = candidate
      canvasStale = false
      reportedHeight = candidate.height
      markDirty(0)
      return
    }
    if (current.fits(width, style)) {
      canvasStale = false
      return
    }
    if (!fallBack) {
      canvasStale = true
      removeCallbacks(settleCanvas)
      post(settleCanvas)
      return
    }
    // No layout for this width or style: lay the text out here.
    canvasStale = false
    canvas = null
    text.append(current.text)
    hadText = true
    rebuildLayout()
    reportHeight()
  }

  private fun releaseSource() {
    base = 0
    bytes = null
    capacity = 0
    source?.release()
    source = null
  }

  /** The stream ended and all of its text is shown: the view no longer reads the buffer. */
  private fun finishSource(committed: Long) {
    // A finished prop text the view laid out itself: kept for a background layout when it leaves.
    if (sourceIsProp && committed <= INSTANT_FILL_BYTES) {
      finishedProp = source
      source = null
    }
    releaseSource()
  }

  private fun releaseFinishedProp() {
    finishedProp?.release()
    finishedProp = null
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
      finishSource(committed)
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
    hadText = true
    markDirty(oldLength)
    if (displayLogKey != null) unloggedShown = offset
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
    // Also under a canvas (no text then): an `attach` later lays out into it.
    rebuildLayout()
    if (canvas != null) {
      syncCanvas(fallBack = false)
      return
    }
    reportHeight()
  }

  /** Returns the time (ns) spent committing the new height, if there was one. */
  private fun reportHeight(): Long {
    if (canvas != null) return 0
    val layout = textLayout ?: return 0
    val started = System.nanoTime()
    // Like <Text>, an empty view has no height (and mounting one doesn't need a commit).
    val px = if (text.isEmpty()) 0 else layout.height
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
    val color = textColor ?: defaultTextColor()
    if (paint.color != color) {
      paint.color = color
      markDirty(0)
    }
    val next = StreamTextStyle(fontSize, fontFamily, fontWeight, lineHeight, letterSpacing, textAlign)
    if (styleApplied && next == style) return
    styleApplied = true
    style = next
    paint = style.paint(context.assets).also { it.color = color }
    if (canvas != null) {
      // The shadow node prepares a layout for the new style: it comes with the state.
      syncCanvas(fallBack = false)
      return
    }
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
    textLayout = if (layoutWidth > 0) style.dynamicLayout(text, paint, layoutWidth) else null
  }

  // MARK: - Drawing

  /** The text from `charOffset` on changed: re-record the blocks it touches. */
  private fun markDirty(charOffset: Int) {
    val layout = textLayout
    val block = if (layout == null || charOffset == 0 || canvas != null) 0 else {
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
    // The text appended since the last frame is drawn now: log it as shown.
    if (unloggedShown >= 0) {
      displayLogKey?.let { StreamTextDebug.appendLog(it, unloggedShown) }
      unloggedShown = -1
    }
    val prepared = this.canvas
    val layout: Layout = prepared?.layout ?: textLayout ?: return
    if (shownText().isEmpty()) return
    // A prepared layout draws with its own paint.
    prepared?.paint?.color = paint.color
    canvas.save()
    canvas.translate(paddingLeft.toFloat(), paddingTop.toFloat())
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
      node.setPosition(0, top, layout.width, bottom)
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
    info.text = accessibilityText()
  }

  private fun accessibilityText(): String {
    val shown = shownText()
    val length = shown.length
    if (length <= ACCESSIBLE_TAIL_CHARS) return shown.toString()
    // Not from the middle of a surrogate pair.
    var start = length - ACCESSIBLE_TAIL_CHARS
    if (Character.isLowSurrogate(shown[start])) start++
    return "…" + shown.subSequence(start, length)
  }

  private companion object {
    const val TAG = "StreamText"
    const val SLICE_BYTES = 16 * 1024
    /** Main-thread time per frame we allow for append + layout (a 60 Hz frame is ~16.7 ms). */
    const val FRAME_BUDGET_NS = 6_000_000.0
    const val MIN_BYTES_PER_TICK = 4 * 1024
    const val MAX_BYTES_PER_TICK = 256 * 1024
    /** As the C++ prepared layouts' limit (StreamTextPreparedAndroid.cpp). */
    const val INSTANT_FILL_BYTES = 64L * 1024
    const val BLOCK_LINES = 32
    const val ACCESSIBLE_TAIL_CHARS = 2000
  }
}
