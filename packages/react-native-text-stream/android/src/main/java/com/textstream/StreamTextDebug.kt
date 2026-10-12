package com.textstream

import java.lang.ref.WeakReference

/**
 * Tests and benchmarks only: views with `recordDisplayLog` by `nativeID`, and when their bytes
 * reached the screen. Inert unless the library is built with STREAMTEXT_TESTING (the example app).
 */
object StreamTextDebug {
  @JvmStatic val enabled: Boolean
    get() = BuildConfig.STREAMTEXT_TESTING

  // Main thread.
  private val views = HashMap<String, WeakReference<StreamTextView>>()

  private val logs = HashMap<String, DoubleArray>()
  private val logSizes = HashMap<String, Int>()

  internal fun register(key: String, view: StreamTextView) {
    views[key] = WeakReference(view)
  }

  internal fun unregister(key: String, view: StreamTextView) {
    if (views[key]?.get() === view) views.remove(key)
  }

  /** The view with `recordDisplayLog` and this `nativeID`. Main thread. */
  @JvmStatic
  fun view(key: String): StreamTextView? = views[key]?.get()

  internal fun appendLog(key: String, bytes: Long) {
    val now = System.currentTimeMillis().toDouble()
    synchronized(logs) {
      val size = logSizes[key] ?: 0
      var log = logs[key] ?: DoubleArray(64)
      if (size + 2 > log.size) log = log.copyOf(log.size * 2)
      log[size] = now
      log[size + 1] = bytes.toDouble()
      logs[key] = log
      logSizes[key] = size + 2
    }
  }

  internal fun clearLog(key: String) {
    synchronized(logs) {
      logs.remove(key)
      logSizes.remove(key)
    }
  }

  /** A view's main-thread cost while it streamed, as iOS's StreamTextRenderStats.Sample (ns). */
  internal class RenderSample {
    var ticks = 0
    var appendNs = 0L
    var layoutNs = 0L
    var commitNs = 0L
    var maxTickNs = 0L
    var heightReports = 0
    var maxTickBytes = 0L
    var slowTicks = 0
    var draws = 0
    var drawNs = 0L
    var maxDrawNs = 0L
  }

  private val renderStats = HashMap<String, DoubleArray>()

  /** The stream ended: its sample, read by benchmarks. */
  internal fun setRenderStats(key: String, s: RenderSample) {
    val ms = { ns: Long -> ns / 1e6 }
    synchronized(renderStats) {
      renderStats[key] = doubleArrayOf(
          s.ticks.toDouble(), ms(s.appendNs), ms(s.layoutNs), ms(s.commitNs), ms(s.maxTickNs),
          s.heightReports.toDouble(), s.maxTickBytes.toDouble(), s.slowTicks.toDouble(), 0.0,
          s.draws.toDouble(), ms(s.drawNs), ms(s.maxDrawNs))
    }
  }

  /**
   * `[ticks, appendMs, layoutMs, commitMs, maxTickMs, heightReports, maxTickBytes, slowTicks, grows,
   * draws, drawMs, maxDrawMs]` (iOS's, then Android's drawing), or empty until the stream finished.
   */
  @JvmStatic
  fun renderStats(key: String): DoubleArray = synchronized(renderStats) { renderStats[key]?.copyOf() ?: DoubleArray(0) }

  /** Phases are timed once [phases] was asked for (benchmarks). */
  @Volatile private var timingPhases = false
  private val phases = HashMap<String, DoubleArray>()

  /** Benchmarks: one run of a named phase. Any thread. */
  internal fun addPhase(name: String, nanos: Long) {
    if (!timingPhases) return
    synchronized(phases) {
      val phase = phases.getOrPut(name) { DoubleArray(2) }
      phase[0] += 1.0
      phase[1] += nanos / 1e6
    }
  }

  internal val timing: Boolean
    get() = timingPhases

  /** `{"<phase>.count": n, "<phase>.ms": total}` since the last reset; turns timing on. */
  @JvmStatic
  fun phases(reset: Boolean): Map<String, Double> {
    if (!enabled) return emptyMap()
    timingPhases = true
    return synchronized(phases) {
      val out = HashMap<String, Double>()
      for ((name, value) in phases) {
        out["$name.count"] = value[0]
        out["$name.ms"] = value[1]
      }
      if (reset) phases.clear()
      out
    }
  }

  private var mainLayouts = 0
  private var mainLongestNs = 0L

  internal fun noteMainLayout(nanos: Long) {
    mainLayouts++
    mainLongestNs = maxOf(mainLongestNs, nanos)
  }

  /** Tests: `[count, longest ms]` of the prepared layouts made on the main thread since the last reset. Main thread. */
  @JvmStatic
  fun mainLayouts(reset: Boolean): DoubleArray {
    val out = doubleArrayOf(mainLayouts.toDouble(), mainLongestNs / 1e6)
    if (reset) {
      mainLayouts = 0
      mainLongestNs = 0
    }
    return out
  }

  /**
   * Tests: the display lists of every view in a window, `[bytes, blocks, canvases, out of sight
   * with blocks]`. Main thread.
   */
  @JvmStatic
  fun blockMemory(): LongArray {
    val out = LongArray(4)
    for (view in StreamTextView.attachedViews) {
      val (count, bytes, hidden) = view.debugBlocks()
      out[0] += bytes
      out[1] += count
      if (view.debugMode == "canvas") out[2]++
      if (hidden == 1L && count > 0) out[3]++
    }
    return out
  }

  /** Off: finished sources are laid out by the view at mount, as before prepared layouts. */
  @JvmStatic
  fun setPreparedLayout(enabled: Boolean) {
    if (this.enabled) StreamTextNative.setPreparedEnabled(enabled)
  }

  /** Forgets every prepared layout, so the next mounts lay out from scratch (a cold start). */
  @JvmStatic
  fun clearPreparedLayouts() {
    if (enabled) StreamTextNative.clearPreparedCache(true)
  }

  @JvmStatic
  fun preparedCacheCount(): Int = if (enabled) StreamTextNative.preparedCacheCount() else 0

  @JvmStatic
  fun preparedCacheDump(): String = if (enabled) StreamTextNative.preparedCacheDump() else ""

  /** What TalkBack reads of the canvas of the view with this key, as JSON (see StreamTextView). Main thread. */
  fun accessibility(key: String, lines: IntArray): String? = view(key)?.debugAccessibility(lines)

  /** `[wallClockMs, bytesShown, ...]` for the view with this key. Any thread. */
  @JvmStatic
  fun log(key: String): DoubleArray =
      synchronized(logs) { logs[key]?.copyOf(logSizes[key] ?: 0) ?: DoubleArray(0) }
}

/** What a view shows, for tests (the example's StreamTextTesting.snapshot). */
class StreamTextDebugState(
    val text: String,
    val height: Float,
    val status: String,
    val hasTextView: Boolean,
    val fontSize: Float,
    /** #RRGGBBAA */
    val color: String,
    val selectable: Boolean,
    val accessibilityValue: String,
    /** "canvas" (a prepared layout), "textView" (the streaming layout), or "" before any text. */
    val mode: String,
    val engine: String,
    /** Display lists the view holds (blocks of lines). */
    val tiles: Int,
)
