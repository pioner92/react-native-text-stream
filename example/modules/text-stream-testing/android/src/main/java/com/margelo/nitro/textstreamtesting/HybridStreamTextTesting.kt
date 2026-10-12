package com.margelo.nitro.textstreamtesting

import android.app.Application
import android.content.ComponentCallbacks2
import android.os.Handler
import android.os.Looper
import com.facebook.proguard.annotations.DoNotStrip
import com.margelo.nitro.NitroModules
import com.margelo.nitro.core.ArrayBuffer
import com.margelo.nitro.core.Promise
import com.textstream.StreamTextDebug
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Tests only: writes TSB1 buffers like the nitro-fetch producer, and reads StreamTextViews. */
@DoNotStrip
class HybridStreamTextTesting : HybridStreamTextTestingSpec() {
  private val lock = Any()
  /**
   * Buffers this object created, by data address: a buffer coming back from JS is looked up here,
   * so writes always go to memory this object owns and keeps alive.
   */
  private val buffers = HashMap<Long, ArrayBuffer>()
  private val producers = ArrayList<ScheduledFuture<*>>()
  private val main = Handler(Looper.getMainLooper())

  override fun createBuffer(capacity: Double): ArrayBuffer {
    val buffer = ArrayBuffer.allocate(TSB_HEADER_SIZE + capacity.toInt())
    val base = TsbWriter.address(buffer.getBuffer(false))
    TsbWriter.init(base, capacity.toLong())
    synchronized(lock) { buffers[base] = buffer }
    return buffer
  }

  private fun owned(buffer: ArrayBuffer): Long {
    val base = TsbWriter.address(buffer.getBuffer(false))
    synchronized(lock) {
      if (!buffers.containsKey(base)) throw IllegalArgumentException("Not a buffer from StreamTextTesting.createBuffer()")
    }
    return base
  }

  override fun append(buffer: ArrayBuffer, bytes: ArrayBuffer) {
    val data = bytes.toByteArray()
    TsbWriter.append(owned(buffer), data, 0, data.size)
  }

  override fun setStatus(buffer: ArrayBuffer, status: Double) {
    TsbWriter.setStatus(owned(buffer), status.toInt())
  }

  override fun startProducer(buffer: ArrayBuffer, bytes: ArrayBuffer, chunk: Double, intervalMs: Double, finalStatus: Double) {
    val base = owned(buffer)
    val data = bytes.toByteArray().copyOf() // JS may reuse its buffer
    val size = maxOf(1, chunk.toInt())
    var offset = 0
    lateinit var future: ScheduledFuture<*>
    future = producerQueue.scheduleAtFixedRate({
      val end = minOf(offset + size, data.size)
      if (end > offset) {
        TsbWriter.append(base, data, offset, end - offset)
        offset = end
      }
      if (offset >= data.size) {
        if (finalStatus > 0) TsbWriter.setStatus(base, finalStatus.toInt())
        future.cancel(false)
      }
    }, 0, maxOf(1L, intervalMs.toLong()), TimeUnit.MILLISECONDS)
    synchronized(lock) { producers.add(future) }
  }

  override fun stopProducers() {
    val stopped = synchronized(lock) { ArrayList(producers).also { producers.clear() } }
    stopped.forEach { it.cancel(false) }
  }

  override fun snapshot(key: String): Promise<StreamTextSnapshot> {
    val promise = Promise<StreamTextSnapshot>()
    main.post {
      val view = StreamTextDebug.view(key)
      if (view == null) {
        promise.reject(IllegalStateException("No StreamTextView with nativeID $key and recordDisplayLog"))
        return@post
      }
      val s = view.debugState()
      promise.resolve(StreamTextSnapshot(
          s.text, s.height.toDouble(), s.status, s.hasTextView, s.fontSize.toDouble(), s.color, s.selectable,
          s.accessibilityValue, s.mode, s.engine, s.tiles.toDouble()))
    }
    return promise
  }

  override fun simulateMemoryWarning() {
    main.post {
      (NitroModules.applicationContext?.applicationContext as? Application)
          ?.onTrimMemory(ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL)
    }
  }

  // No text view pool on Android: views lay out their own text.
  override fun drainTextViewPool() {}

  // Not on Android yet: the features they test come later (selection, tiles, ...).

  override fun setPreparedLayout(enabled: Boolean) {
    StreamTextDebug.setPreparedLayout(enabled)
  }

  override fun setPreparedCoreText(enabled: Boolean) {}

  override fun clearPreparedLayouts() {
    StreamTextDebug.clearPreparedLayouts()
  }

  override fun preparedCacheCount(): Double = StreamTextDebug.preparedCacheCount().toDouble()

  override fun preparedCacheDump(): String = StreamTextDebug.preparedCacheDump()

  override fun compareEngines(text: String, style: StreamTextTestStyle, width: Double): Map<String, Double> = emptyMap()

  override fun resizeSurface(keys: Array<String>, width: Double, ms: Double): Promise<String> = unsupported()

  override fun tileMemory(): Promise<String> = unsupported()

  override fun cover(shown: Boolean): Promise<Double> = unsupported()

  override fun measureMount(text: String, width: Double): Promise<Map<String, Double>> = unsupported()

  override fun selectWordAt(key: String, x: Double, y: Double): Promise<String> {
    val promise = Promise<String>()
    main.post {
      val view = StreamTextDebug.view(key)
      if (view == null) {
        promise.reject(IllegalStateException("No StreamTextView with nativeID $key and recordDisplayLog"))
      } else {
        promise.resolve(view.debugSelectWordAt(x.toFloat(), y.toFloat()))
      }
    }
    return promise
  }

  override fun selectionGeometry(key: String, location: Double, length: Double): Promise<String> = unsupported()

  override fun accessibility(key: String, lines: DoubleArray): Promise<String> = unsupported()

  private fun <T> unsupported(): Promise<T> = Promise.rejected(UnsupportedOperationException("Not on Android yet"))

  private companion object {
    const val TSB_HEADER_SIZE = 64
    val producerQueue = Executors.newSingleThreadScheduledExecutor { Thread(it, "StreamText.testProducer") }
  }
}
