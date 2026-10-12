package com.textstream

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Matrix
import android.graphics.RectF
import android.os.Build
import android.os.Bundle
import android.text.Layout
import android.text.TextPaint
import android.util.Log
import android.util.TypedValue
import android.view.Choreographer
import android.view.GestureDetector
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewTreeObserver
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityManager
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.HorizontalScrollView
import android.widget.ScrollView
import androidx.annotation.RequiresApi
import com.facebook.react.bridge.ReactContext
import com.facebook.react.uimanager.PixelUtil
import com.facebook.react.uimanager.StateWrapper
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.views.view.ReactViewGroup
import java.nio.ByteBuffer
import org.json.JSONArray
import org.json.JSONObject
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
    set(value) {
      field = value
      if (!value) selection.dismiss()
    }
  var selectionColor: Int? = null

  private val selection = StreamTextSelection(this)
  /** The rest of a long press goes to the selection overlay (dragging out of it). */
  private var forwardingToSelection = false
  private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
    override fun onDown(e: MotionEvent) = true

    override fun onLongPress(e: MotionEvent) {
      if (!selectable || shownText().isEmpty()) return
      selection.start(e, shownText(), style, paint, selectionColor)
      forwardingToSelection = true
      performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
    }
  })

  /** Tests and benchmarks (STREAMTEXT_TESTING): `nativeID` with `recordDisplayLog`. */
  var recordDisplayLog = false
  private var displayLogKey: String? = null
  /** Bytes appended and not yet drawn: logged when they are. */
  private var unloggedShown = -1L
  /** Benchmarks (with a display log key): main-thread cost of the stream being shown. */
  private var stats: StreamTextDebug.RenderSample? = null
  /** Text was laid out here at least once (the iOS text view exists). */
  private var hadText = false

  private var style = StreamTextStyle()
  private var styleApplied = false
  private var paint = TextPaint(Paint.ANTI_ALIAS_FLAG)
  /** The text the view lays out itself: streamed, attached, or a canvas it fell back from. */
  private val flow = StreamTextFlow()
  /** Content width in px. */
  private var layoutWidth = -1

  /** A prepared layout shown as is; then [flow] stays empty. */
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
  /** A finished prop source shown in [flow], for a background layout when the view goes. */
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
  /** The streamFetch() buffer last attached (0: another kind), kept once let go of: not attached twice. */
  private var attachedSerial = 0L
  /** The `streamId` prop, listened to: a request started (or replaced) under it later is shown too. */
  private var streamId: String? = null
  private var streamToken = 0L
  /** The JS wrapper's generation of the attached source, sent back with each status event. */
  private var generation = 0
  private var revision = 0
  /** The text height in dp, as last committed. Starts at the shadow node state's initial height. */
  private var reportedHeight = 0f
  private var heightPending = false
  private var reportedStatus: String? = null

  // A canvas's blocks of lines, each its own display list, recorded near the screen (API 29+);
  // the flow has its own.
  private val blocks = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) StreamTextBlocks() else null
  /** The nearest scroll view, whose bounds are what the view can show; else the window. */
  private var scrollAncestor: View? = null
  private val toWindow = Matrix()
  private val fromWindow = Matrix()
  private val viewportRect = RectF()
  private val scrollListener = ViewTreeObserver.OnScrollChangedListener {
    aheadPaused = false
    if (needsRedraw()) invalidate()
  }
  /** After a memory trim, nothing is drawn ahead of the screen until it scrolls. */
  private var aheadPaused = false

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
  /** Shows whatever streams under `id` from now on; null stops. */
  fun setStreamId(id: String?) {
    if (id == streamId) return
    streamId?.let { StreamTextNative.unlistenStream(it, streamToken) }
    streamId = id
    if (id == null) return
    val wake = Runnable { post { streamPublished() } }
    streamToken = StreamTextNative.listenStream(id, wake)
    // Published between the props' lookup and now: not missed.
    wake.run()
  }

  /** A request streams under `streamId` (a new one, or one that replaced it). Main thread. */
  private fun streamPublished() {
    val id = streamId ?: return
    val source = StreamTextNative.streamSource(id) ?: return
    // Already shown (the state's own lookup attached it): attaching again would restart it.
    if (source.serial() == attachedSerial) {
      source.release()
      return
    }
    attach(source, propGeneration, null)
  }

  fun attachCommand(bufferId: Int, generation: Int) {
    attach(if (bufferId > 0) StreamTextNative.takeBuffer(bufferId) else null, generation, null)
  }

  fun clearCommand(generation: Int) {
    attach(null, generation, null)
  }

  /** The view is gone: stop reading the buffer and let go of it. */
  fun dispose() {
    setStreamId(null)
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
  /** Tests: "canvas", "textView" or "" (nothing shown yet). */
  val debugMode: String
    get() = if (canvas != null) "canvas" else if (hadText) "textView" else ""

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
        accessibilityValue = reading().text,
        mode = debugMode,
        engine = if (canvas != null) "staticLayout" else "",
        tiles = if (canvas != null) blocks?.count ?: 0 else flow.blockCount,
    )
  }

  private fun shownText(): CharSequence = canvas?.text ?: flow.text

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
    selection.dismiss()
    sourceIsProp = fromProp
    attachedSerial = 0
    // A new source starts its own status sequence (a finished one reports only its final status).
    reportedStatus = null
    this.generation = generation
    releaseSource()
    offset = 0
    finished = false
    decoder.reset()
    unloggedShown = -1
    displayLogKey?.let { StreamTextDebug.clearLog(it) }
    stats = if (displayLogKey != null) StreamTextDebug.RenderSample() else null
    canvasStale = false
    if (canvas != null) {
      canvas = null
      markDirty(0)
    }
    if (flow.text.isNotEmpty()) {
      flow.clear()
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
      attachedSerial = newSource.serial()
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
        if (isAttachedToWindow) watcher.start(address, 0, newSource)
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
    flow.clear()
    flow.text.append(current.text)
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
    attachedViews.add(this)
    scrollAncestor = generateSequence(parent) { it.parent }.firstOrNull {
      it is ScrollView || it is HorizontalScrollView
    } as? View
    if (blocks != null) viewTreeObserver.addOnScrollChangedListener(scrollListener)
    if (base != 0L && !finished) {
      watcher.start(base, offset, source)
      scheduleFrame()
    }
  }

  override fun onDetachedFromWindow() {
    super.onDetachedFromWindow()
    attachedViews.remove(this)
    if (blocks != null) viewTreeObserver.removeOnScrollChangedListener(scrollListener)
    scrollAncestor = null
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
      val appendTime = System.nanoTime() - started
      // Before the first layout there is no width: the text is laid out once the view gets one.
      val measured = flow.isLaidOut
      val commitTime = if (fillAll) 0L else reportHeight()
      val tickTime = System.nanoTime() - started
      stats?.let {
        it.ticks++
        it.appendNs += appendTime
        it.commitNs += commitTime
        it.layoutNs += tickTime - appendTime - commitTime
        it.maxTickNs = max(it.maxTickNs, tickTime)
        it.maxTickBytes = max(it.maxTickBytes, appended)
        if (tickTime > 8_000_000) it.slowTicks++
      }
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
      displayLogKey?.let { key -> stats?.let { StreamTextDebug.setRenderStats(key, it) } }
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
    // With its line height spans, so the re-wrap the append triggers already sees them.
    val spanned = style.withLineHeight(chars)
    if (flow.append(chars, spanned)) flow.freezeIfLong(style, paint) else rebuildLayout()
    selection.append(spanned)
    hadText = true
    invalidate()
    if (displayLogKey != null) unloggedShown = offset
  }

  // MARK: - Selection

  @SuppressLint("ClickableViewAccessibility")
  override fun onTouchEvent(event: MotionEvent): Boolean {
    if (forwardingToSelection) {
      selection.forward(event)
      if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
        forwardingToSelection = false
      }
      return true
    }
    if (!selectable || shownText().isEmpty()) return super.onTouchEvent(event)
    gestures.onTouchEvent(event)
    return true
  }

  /** Tests: selects the word at (`x`, `y`) dp as a long press would, and returns it. */
  fun debugSelectWordAt(x: Float, y: Float): String {
    if (shownText().isEmpty()) return ""
    return selection.selectWordAt(
        PixelUtil.toPixelFromDIP(x), PixelUtil.toPixelFromDIP(y), shownText(), style, paint, selectionColor)
  }

  /**
   * Tests: selects `length` characters from `location` in the overlay a long press lays over the
   * view, and returns, in dp in the view, the selection's rects there and those of the same
   * characters as the view draws them (the overlay draws the text instead while it is up), as JSON
   * `{ text, rtl, rects: [{ x, y, w, h, rtl }], drawn: [...] }`, a rect a line.
   */
  fun debugSelectionGeometry(location: Int, length: Int): String {
    val shown = shownText()
    if (shown.isEmpty()) return "{}"
    val overlay = selection.open(shown, style, paint, selectionColor)
    try {
      val end = location + length
      android.text.Selection.setSelection(overlay.text as android.text.Spannable, location, end)
      val layout = overlay.layout
      val rects = JSONArray()
      selectionRects(layout, location, end, overlay.totalPaddingLeft, overlay.totalPaddingTop, rects)
      val drawn = JSONArray()
      var base = 0
      forEachShownSection { section, lines, y, _ ->
        val sectionEnd = base + section.getLineStart(lines)
        val from = max(location, base)
        val to = min(end, sectionEnd)
        if (from < to) selectionRects(section, from - base, to - base, paddingLeft, paddingTop + y, drawn)
        base = sectionEnd
      }
      return JSONObject()
          .put("text", overlay.text.subSequence(overlay.selectionStart, overlay.selectionEnd).toString())
          .put("rtl", layout.getParagraphDirection(layout.getLineForOffset(location)) == Layout.DIR_RIGHT_TO_LEFT)
          .put("rects", rects)
          .put("drawn", drawn)
          .toString()
    } finally {
      selection.dismiss()
    }
  }

  private fun selectionRects(layout: Layout, start: Int, end: Int, left: Int, top: Int, out: JSONArray) {
    val dp = { px: Float -> PixelUtil.toDIPFromPixel(px).toDouble() }
    val path = android.graphics.Path()
    val bounds = RectF()
    for (line in layout.getLineForOffset(start)..layout.getLineForOffset(end)) {
      val from = max(start, layout.getLineStart(line))
      val to = min(end, layout.getLineEnd(line))
      if (from >= to) continue
      path.reset()
      layout.getSelectionPath(from, to, path)
      path.computeBounds(bounds, true)
      if (bounds.isEmpty) continue
      out.put(JSONObject()
          .put("x", dp(left + bounds.left)).put("y", dp(top + bounds.top))
          .put("w", dp(bounds.width())).put("h", dp(bounds.height()))
          .put("rtl", layout.isRtlCharAt(from)))
    }
  }

  // MARK: - Measuring

  override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
    super.onSizeChanged(w, h, oldw, oldh)
    if (initialized) selection.layoutOverlay()
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
    if (canvas != null || !flow.isLaidOut) return 0
    val started = System.nanoTime()
    // Like <Text>, an empty view has no height (and mounting one doesn't need a commit).
    val px = flow.height
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
    stats?.let { it.heightReports++ }
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
    if (styleApplied) selection.dismiss()
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
    flow.rebuild(style, paint, layoutWidth)
  }

  // MARK: - Drawing

  /** Every line changed (a new text, color or layout): re-record every block. Appends mark their own. */
  private fun markDirty(@Suppress("UNUSED_PARAMETER") charOffset: Int) {
    blocks?.markDirty(0)
    flow.markAllDirty()
    invalidate()
  }

  private fun discardBlocks() {
    blocks?.discard()
    flow.discardBlocks()
  }

  override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
    super.onLayout(changed, left, top, right, bottom)
    // Moved within its scroll view (a bubble above grew): other blocks may be near the screen now.
    if (changed && needsRedraw()) invalidate()
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)
    // The text appended since the last frame is drawn now: log it as shown.
    if (unloggedShown >= 0) {
      displayLogKey?.let { StreamTextDebug.appendLog(it, unloggedShown) }
      unloggedShown = -1
    }
    if (selection.overlay != null) return
    val drawStarted = if (stats != null) System.nanoTime() else 0L
    if (shownText().isEmpty()) return
    val prepared = this.canvas
    if (prepared == null && !flow.isLaidOut) return
    // A prepared layout draws with its own paint.
    prepared?.paint?.color = paint.color
    canvas.save()
    canvas.translate(paddingLeft.toFloat(), paddingTop.toFloat())
    val blocked = canvas.isHardwareAccelerated && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
    val drawSection = { layout: Layout, lines: Int, y: Int, sectionBlocks: StreamTextBlocks? ->
      canvas.save()
      canvas.translate(0f, y.toFloat())
      if (sectionBlocks != null && blocked && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        sectionBlocks.draw(
            canvas, layout, lines, nearScreen(layout, lines, y, aheadDp), nearScreen(layout, lines, y, keepDp))
      } else {
        // Without display lists (before API 29): the lines this section shows.
        canvas.clipRect(0, 0, layout.width, layout.getLineTop(lines))
        layout.draw(canvas)
      }
      canvas.restore()
    }
    forEachShownSection(drawSection)
    canvas.restore()
    stats?.let {
      val time = System.nanoTime() - drawStarted
      it.draws++
      it.drawNs += time
      it.maxDrawNs = max(it.maxDrawNs, time)
    }
  }

  /** A scroll or a move brought other blocks near the screen than the ones drawn. */
  private fun needsRedraw(): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q || shownText().isEmpty()) return false
    var redraw = false
    val check = { layout: Layout, lines: Int, y: Int, sectionBlocks: StreamTextBlocks? ->
      if (sectionBlocks != null && !redraw) {
        val near = nearScreen(layout, lines, y, aheadDp)
        redraw = if (near.isEmpty()) sectionBlocks.drawnLast >= sectionBlocks.drawnFirst
        else near.first != sectionBlocks.drawnFirst || near.last != sectionBlocks.drawnLast
      }
    }
    forEachShownSection(check)
    return redraw
  }

  private val aheadDp: Float
    get() = if (aheadPaused) 0f else AHEAD_DP
  private val keepDp: Float
    get() = if (aheadPaused) 0f else KEEP_DP

  /**
   * Memory is short: all but the blocks on screen go, and none is drawn ahead until a scroll. (A
   * hidden window's display lists HWUI frees itself, and those of views it no longer draws.)
   */
  private fun trimMemory() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
    aheadPaused = true
    forEachShownSection { layout, lines, y, sectionBlocks -> sectionBlocks?.keepOnly(nearScreen(layout, lines, y, 0f)) }
    // The next draw draws the screen only, so a scroll after it draws ahead again.
    invalidate()
  }

  /** Tests: the display lists held, `[count, bytes]`, and whether the view is out of sight. Main thread. */
  internal fun debugBlocks(): LongArray {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return longArrayOf(0, 0, 0)
    var count = 0L
    var bytes = 0L
    forEachShownSection { _, _, _, sectionBlocks ->
      count += sectionBlocks?.count ?: 0
      bytes += sectionBlocks?.bytes ?: 0L
    }
    return longArrayOf(count, bytes, if (isShown && windowVisibility == VISIBLE) 0 else 1)
  }

  /** The canvas, or the sections of the text laid out here: (layout, lines shown, y of its top, blocks). */
  private fun forEachShownSection(action: (layout: Layout, lines: Int, y: Int, blocks: StreamTextBlocks?) -> Unit) {
    val prepared = canvas
    if (prepared != null) action(prepared.layout, prepared.layout.lineCount, 0, blocks) else flow.forEachSection(action)
  }

  /** The blocks of `layout`'s first `lines`, drawn `y` px below the text's top, within `margin` dp of the screen. */
  @RequiresApi(Build.VERSION_CODES.Q)
  private fun nearScreen(layout: Layout, lines: Int, y: Int, margin: Float): IntRange {
    val viewport = viewport() ?: return IntRange.EMPTY
    val px = PixelUtil.toPixelFromDIP(margin)
    val top = (viewport.top - paddingTop - y - px).toInt()
    val bottom = (viewport.bottom - paddingTop - y + px).toInt()
    return StreamTextBlocks.range(layout, lines, top, bottom)
  }

  /**
   * What the view's scroll view (or the window) shows, in the view's coordinates, transforms
   * included (an inverted scroll view). Main thread; the rect is reused.
   */
  @RequiresApi(Build.VERSION_CODES.Q)
  private fun viewport(): RectF? {
    val clip = scrollAncestor ?: rootView ?: return null
    toWindow.reset()
    transformMatrixToGlobal(toWindow)
    if (!toWindow.invert(fromWindow)) return null
    toWindow.reset()
    clip.transformMatrixToGlobal(toWindow)
    viewportRect.set(0f, 0f, clip.width.toFloat(), clip.height.toFloat())
    toWindow.mapRect(viewportRect)
    fromWindow.mapRect(viewportRect)
    return viewportRect
  }

  // MARK: - Accessibility

  /** TalkBack's cursor in [readingText] (-1: none yet), as a View keeps one for its iterators. */
  private var readingCursor = -1
  private var readingText = ""
  /** The segment the last move went over: for tests. */
  private var lastTraversed: IntArray? = null

  override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
    super.onInitializeAccessibilityNodeInfo(info)
    val text = reading().text
    info.text = text
    if (text.isNotEmpty()) {
      info.movementGranularities = StreamTextReading.GRANULARITIES
      info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_NEXT_AT_MOVEMENT_GRANULARITY)
      info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY)
    }
  }

  override fun performAccessibilityAction(action: Int, arguments: Bundle?): Boolean {
    when (action) {
      AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
      AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY -> {
        val granularity = arguments?.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT) ?: return false
        return traverse(granularity, action == AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY)
      }
      AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS -> readingCursor = -1
    }
    return super.performAccessibilityAction(action, arguments)
  }

  /** Moves TalkBack's cursor over the next or previous segment, as View.traverseAtGranularity. */
  private fun traverse(granularity: Int, forward: Boolean): Boolean {
    val reading = reading()
    val text = reading.text
    if (text != readingText) {
      readingText = text
      readingCursor = -1
    }
    if (text.isEmpty()) return false
    val current = if (readingCursor >= 0) readingCursor else if (forward) 0 else text.length
    val range = (if (forward) reading.following(granularity, current) else reading.preceding(granularity, current)) ?: return false
    readingCursor = if (forward) range[1] else range[0]
    lastTraversed = range
    val manager = context.getSystemService(Context.ACCESSIBILITY_SERVICE) as? AccessibilityManager
    if (manager?.isEnabled == true) {
      @Suppress("DEPRECATION")
      val event = AccessibilityEvent.obtain(AccessibilityEvent.TYPE_VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY)
      onInitializeAccessibilityEvent(event)
      event.text.add(text)
      event.fromIndex = range[0]
      event.toIndex = range[1]
      event.action = if (forward) AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY
      else AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY
      event.movementGranularity = granularity
      sendAccessibilityEventUnchecked(event)
    }
    return true
  }

  /**
   * What accessibility reads: a canvas, the lines on screen, whole, as iOS does; the text laid out
   * here (a stream), its end: a snapshot of megabytes of text would stall the main thread.
   */
  private fun reading(): StreamTextReading {
    val prepared = canvas
    if (prepared != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      val layout = prepared.layout
      val viewport = viewport()
      if (viewport != null && layout.lineCount > 0) {
        val top = (viewport.top - paddingTop).toInt()
        val bottom = (viewport.bottom - paddingTop).toInt()
        if (bottom < 0 || top > layout.height) return StreamTextReading.EMPTY
        val first = layout.getLineForVertical(max(top, 0))
        val last = layout.getLineForVertical(min(bottom, layout.height))
        val from = layout.getLineStart(first)
        return StreamTextReading(
            prepared.text.substring(from, layout.getLineEnd(last)), IntArray(last - first + 1) { layout.getLineStart(first + it) - from })
      }
    }
    return tailReading()
  }

  /** The text's last [ACCESSIBLE_TAIL_CHARS] characters, after a "…" when there is more. */
  private fun tailReading(): StreamTextReading {
    val shown = shownText()
    val length = shown.length
    var start = max(length - ACCESSIBLE_TAIL_CHARS, 0)
    // Not from the middle of a surrogate pair.
    if (start > 0 && Character.isLowSurrogate(shown[start])) start++
    val prefix = if (start > 0) "…" else ""
    val starts = ArrayList<Int>()
    var base = 0
    forEachShownSection { layout, lines, _, _ ->
      val end = base + layout.getLineStart(lines)
      if (end > start) {
        for (line in layout.getLineForOffset(max(start - base, 0)) until lines) {
          starts.add(prefix.length + max(base + layout.getLineStart(line) - start, 0))
        }
      }
      base = end
    }
    return StreamTextReading(prefix + shown.subSequence(start, length), starts.toIntArray())
  }

  /**
   * Tests: what TalkBack reads of a canvas, as iOS's debugAccessibility: the node's text, the
   * `lines` asked for with the line found at the middle of each, the line at the top of the screen,
   * and the first lines TalkBack moves over line by line.
   */
  internal fun debugAccessibility(lines: IntArray): String {
    val prepared = canvas ?: return "{}"
    val layout = prepared.layout
    val info = createAccessibilityNodeInfo()
    val value = info.text?.toString() ?: ""
    @Suppress("DEPRECATION")
    info.recycle()
    val picked = JSONArray()
    for (line in lines) {
      val item = JSONObject().put("line", line)
      if (line in 0 until layout.lineCount) {
        item.put("text", prepared.text.substring(layout.getLineStart(line), layout.getLineEnd(line)))
        item.put("back", layout.getLineForVertical((layout.getLineTop(line) + layout.getLineBottom(line)) / 2))
      } else {
        item.put("text", JSONObject.NULL).put("back", JSONObject.NULL)
      }
      picked.put(item)
    }
    val top = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) viewport()?.let { (it.top - paddingTop).toInt() } ?: 0 else 0
    val traversed = JSONArray()
    val (cursor, text) = readingCursor to readingText
    readingCursor = -1
    val byLine = Bundle().apply {
      putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT, AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE)
    }
    repeat(3) {
      if (performAccessibilityAction(AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, byLine)) {
        val range = lastTraversed!!
        traversed.put(readingText.substring(range[0], range[1]))
      }
    }
    readingCursor = cursor
    readingText = text
    return JSONObject()
        .put("value", value)
        .put("page", value)
        .put("count", layout.lineCount)
        .put("lines", picked)
        .put("firstLine", layout.getLineForVertical(max(top, 0)))
        .put("traversed", traversed)
        .toString()
  }

  internal companion object {
    /** Views in a window: those a memory trim reaches. Main thread. */
    val attachedViews = HashSet<StreamTextView>()

    /** A memory trim (StreamTextPreparedLayout's callbacks). Main thread. */
    fun trimAllMemory() {
      for (view in attachedViews) view.trimMemory()
    }

    const val TAG = "StreamText"
    const val SLICE_BYTES = 16 * 1024
    /** Main-thread time per frame we allow for append + layout (a 60 Hz frame is ~16.7 ms). */
    const val FRAME_BUDGET_NS = 6_000_000.0
    const val MIN_BYTES_PER_TICK = 4 * 1024
    const val MAX_BYTES_PER_TICK = 256 * 1024
    /** As the C++ prepared layouts' limit (StreamTextPreparedAndroid.cpp). */
    const val INSTANT_FILL_BYTES = 64L * 1024
    /** Drawn ahead of the screen, and kept, above and below (iOS: 512 and 1024 pt). */
    const val AHEAD_DP = 512f
    const val KEEP_DP = 1024f
    const val ACCESSIBLE_TAIL_CHARS = 2000
  }
}
