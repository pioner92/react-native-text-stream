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
