package com.textstream

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Process
import android.os.SystemClock

/**
 * Wakes the view when bytes or a final status land in its buffer. A buffer from streamFetch()
 * tells it on every write (no polling); any other TSB1 buffer is watched off the main thread:
 * polled fast while data flows, backing off while the producer is quiet (a model "thinking"),
 * stopping once the status is final. Same policy as the iOS BufferWatcher.
 */
internal class StreamTextWatcher(private val wake: Runnable) {
  private val lock = Any()
  // Guarded by `lock`.
  private var base = 0L
  private var seenBytes = 0L
  private var seenFinal = false
  private var wakePending = false
  private var lastChange = 0L
  private var session = 0
  // Main thread.
  private var observed: StreamTextSource? = null
  private var observerToken = 0L

  /** Watches `base` (`source`'s) for bytes beyond `from`. Main thread. */
  fun start(base: Long, from: Long, source: StreamTextSource?) {
    stop()
    val started: Int
    synchronized(lock) {
      this.base = base
      seenBytes = from
      seenFinal = false
      wakePending = false
      lastChange = SystemClock.uptimeMillis()
      started = ++session
    }
    if (source != null && source.isPushed()) {
      observerToken = source.observe { pushed(started) }
      observed = source
    } else {
      handler.postDelayed({ poll(started) }, 8)
    }
  }

  /** After this returns, the watcher no longer reads the buffer. Main thread. */
  fun stop() {
    synchronized(lock) {
      base = 0
      session++
    }
    observed?.unobserve(observerToken)
    observed = null
  }

  /** The writer published bytes or the final status; on its thread. */
  private fun pushed(started: Int) {
    synchronized(lock) {
      if (started != session || base == 0L || wakePending) return
      wakePending = true
    }
    mainHandler.post(wake)
  }

  /** The view handled the wake; the next change wakes it again. Main thread. */
  fun didWake() {
    synchronized(lock) { wakePending = false }
  }

  private fun poll(polled: Int) {
    var shouldWake = false
    var nextMs = 0L
    synchronized(lock) {
      if (polled != session || base == 0L) return
      // Status first: once it is terminal, the committed value read after it is final.
      val final = StreamTextNative.status(base) != StreamTextNative.STATUS_OPEN
      val committed = StreamTextNative.committed(base)
      val now = SystemClock.uptimeMillis()
      var changed = false
      if (committed > seenBytes) {
        seenBytes = committed
        changed = true
      }
      if (final && !seenFinal) {
        seenFinal = true
        changed = true
      }
      if (changed) lastChange = now
      if (!seenFinal) {
        // 8 ms while data flows, then 16 ms, then 50 ms once the producer has been quiet for 5 s.
        val quiet = now - lastChange
        nextMs = if (quiet < 1000) 8 else if (quiet < 5000) 16 else 50
      }
      if (changed && !wakePending) {
        wakePending = true
        shouldWake = true
      }
    }
    if (shouldWake) mainHandler.post(wake)
    if (nextMs > 0) handler.postDelayed({ poll(polled) }, nextMs)
  }

  companion object {
    private val handler: Handler by lazy {
      val thread = HandlerThread("StreamText.watcher", Process.THREAD_PRIORITY_DISPLAY)
      thread.start()
      Handler(thread.looper)
    }
    private val mainHandler = Handler(Looper.getMainLooper())
  }
}
