package com.margelo.nitro.textstreamtesting

import android.hardware.display.DisplayManager
import android.os.Debug
import android.view.Display
import com.margelo.nitro.NitroModules
import android.os.Handler
import android.os.Looper
import android.view.Choreographer
import com.facebook.proguard.annotations.DoNotStrip
import com.facebook.react.bridge.UiThreadUtil
import com.margelo.nitro.core.Promise
import com.textstream.StreamTextDebug
import kotlin.math.max
import kotlin.math.roundToInt

/** UI-thread frame pacing with Choreographer, as the iOS one does with a CADisplayLink. */
@DoNotStrip
class HybridFrameMonitor : HybridFrameMonitorSpec() {
  private val lock = Any()
  private var startedAt = 0L
  private var lastFrameNanos = 0L
  private var frames = 0
  private var hitches = 0
  private var droppedFrames = 0
  private var maxFrameNanos = 0L

  // Main thread only.
  private var running = false
  private val waiters = ArrayList<(Double) -> Unit>()
  private val idleWaiters = ArrayList<Pair<Int, (Double) -> Unit>>()
  private var onTimeStreak = 0
  private var streakStartedAt = 0.0
  private val main = Handler(Looper.getMainLooper())

  private val callback = object : Choreographer.FrameCallback {
    override fun doFrame(frameTimeNanos: Long) {
      if (!running) return
      tick(frameTimeNanos)
      Choreographer.getInstance().postFrameCallback(this)
    }
  }

  override fun start() {
    synchronized(lock) {
      frames = 0
      hitches = 0
      droppedFrames = 0
      maxFrameNanos = 0
      lastFrameNanos = 0
      startedAt = System.nanoTime()
    }
    main.post {
      if (running) return@post
      running = true
      Choreographer.getInstance().postFrameCallback(callback)
    }
  }

  override fun stop(): Promise<FrameStats> {
    val promise = Promise<FrameStats>()
    onNextFrame {
      running = false
      Choreographer.getInstance().removeFrameCallback(callback)
      promise.resolve(snapshot())
    }
    return promise
  }

  override fun markPainted(): Promise<Double> {
    val promise = Promise<Double>()
    onNextFrame { promise.resolve(it) }
    return promise
  }

  override fun waitIdle(frames: Double): Promise<Double> {
    val promise = Promise<Double>()
    main.post {
      if (!running) {
        promise.resolve(nowMs())
      } else {
        // Count only frames after this call.
        onTimeStreak = 0
        idleWaiters.add(frames.toInt() to { at: Double -> promise.resolve(at) })
      }
    }
    return promise
  }

  override fun streamTextDisplayLog(key: String): DoubleArray = StreamTextDebug.log(key)

  // Not recorded on Android yet.
  override fun streamTextRenderStats(key: String): DoubleArray = DoubleArray(0)

  override fun streamTextTotals(reset: Boolean): DoubleArray = DoubleArray(0)

  override fun streamTextPhases(reset: Boolean): Map<String, Double> = emptyMap()

  /** The app's proportional set size, the closest to iOS's footprint. */
  override fun memoryFootprintMb(): Double = Debug.getPss() / 1024.0

  /** Runs `body` on the first frame after the main thread drains the work queued before this call. */
  private fun onNextFrame(body: (Double) -> Unit) {
    main.post { if (running) waiters.add(body) else body(nowMs()) }
  }

  private fun snapshot(): FrameStats =
      synchronized(lock) {
        FrameStats(
            frames.toDouble(),
            hitches.toDouble(),
            droppedFrames.toDouble(),
            maxFrameNanos / 1e6,
            (System.nanoTime() - startedAt) / 1e6,
        )
      }

  private fun tick(frameTimeNanos: Long) {
    UiThreadUtil.assertOnUiThread()
    val budget = (1e9 / displayRefreshRate()).toLong()
    var onTime = true
    synchronized(lock) {
      if (lastFrameNanos > 0 && budget > 0) {
        val interval = frameTimeNanos - lastFrameNanos
        maxFrameNanos = max(maxFrameNanos, interval)
        if (interval > budget * 1.5) {
          onTime = false
          hitches++
          droppedFrames += max(0, (interval.toDouble() / budget).roundToInt() - 1)
        }
      }
      lastFrameNanos = frameTimeNanos
      frames++
    }
    val now = nowMs()
    if (onTime) {
      if (onTimeStreak == 0) streakStartedAt = now
      onTimeStreak++
    } else {
      onTimeStreak = 0
    }
    if (idleWaiters.isNotEmpty()) {
      val streak = onTimeStreak
      val idleSince = streakStartedAt
      idleWaiters.removeAll { (needed, resolve) ->
        if (streak < needed) return@removeAll false
        resolve(idleSince)
        true
      }
    }
    if (waiters.isEmpty()) return
    val ready = ArrayList(waiters)
    waiters.clear()
    ready.forEach { it(now) }
  }

  /** The display's current rate: the frame budget, as CADisplayLink's target timestamp gives it. */
  private fun displayRefreshRate(): Float {
    val manager = NitroModules.applicationContext?.getSystemService(DisplayManager::class.java)
    return manager?.getDisplay(Display.DEFAULT_DISPLAY)?.refreshRate ?: 60f
  }

  private fun nowMs(): Double = System.currentTimeMillis().toDouble()
}
