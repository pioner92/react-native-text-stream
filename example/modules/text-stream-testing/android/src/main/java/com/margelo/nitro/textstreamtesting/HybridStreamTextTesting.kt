package com.margelo.nitro.textstreamtesting

import android.app.ActivityOptions
import android.app.Application
import android.content.ComponentCallbacks2
import android.content.Intent
import android.os.Debug
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import com.facebook.proguard.annotations.DoNotStrip
import com.facebook.react.ReactRootView
import com.facebook.react.bridge.ReactContext
import com.facebook.react.uimanager.PixelUtil
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.uimanager.common.UIManagerType
import com.facebook.react.fabric.FabricUIManager
import com.margelo.nitro.NitroModules
import com.margelo.nitro.core.ArrayBuffer
import com.margelo.nitro.core.Promise
import com.textstream.StreamTextDebug
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import kotlin.math.roundToInt
import org.json.JSONArray
import org.json.JSONObject

/** Tests only: writes TSB1 buffers like streamFetch, and reads StreamTextViews. */
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

  override fun textAttributes(key: String): Promise<String> {
    val promise = Promise<String>()
    main.post {
      val view = StreamTextDebug.view(key)
      if (view == null) promise.reject(IllegalStateException("No StreamTextView with nativeID $key"))
      else promise.resolve(view.debugTextAttributes())
    }
    return promise
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

  override fun resizeSurface(keys: Array<String>, width: Double, ms: Double): Promise<String> {
    val promise = Promise<String>()
    main.post {
      val views = keys.mapNotNull { StreamTextDebug.view(it) }
      // The root view a rotation or a window resize lays out again, on the main thread.
      val host = views.firstOrNull()?.let { view ->
        generateSequence(view.parent) { it.parent }.firstOrNull { it is ReactRootView } as? View
      }
      if (keys.isEmpty() || views.size != keys.size || host == null) {
        promise.reject(IllegalStateException("No views ${keys.toList()} with recordDisplayLog in a surface"))
        return@post
      }
      val frames = HashMap<String, JSONArray>()
      val last = HashMap<String, String>()
      fun sample() {
        for ((key, view) in keys.zip(views)) {
          val entry = JSONObject()
              .put("w", PixelUtil.toDIPFromPixel(view.width.toFloat()).toDouble())
              .put("h", PixelUtil.toDIPFromPixel(view.height.toFloat()).toDouble())
              .put("mode", view.debugMode)
          if (last[key] == entry.toString()) continue
          last[key] = entry.toString()
          frames.getOrPut(key) { JSONArray() }.put(entry)
        }
      }
      StreamTextDebug.mainLayouts(true)
      val started = System.nanoTime()
      val px = if (width > 0) PixelUtil.toPixelFromDIP(width).roundToInt() else (host.parent as View).width
      host.layoutParams = host.layoutParams.apply {
        this.width = if (width > 0) px else ViewGroup.LayoutParams.MATCH_PARENT
      }
      // Now, as iOS's layoutIfNeeded: the surface's commit and mount run inside the root's measure.
      host.measure(
          View.MeasureSpec.makeMeasureSpec(px, View.MeasureSpec.EXACTLY),
          View.MeasureSpec.makeMeasureSpec(host.height, View.MeasureSpec.EXACTLY))
      host.layout(host.left, host.top, host.left + px, host.top + host.height)
      val mainMs = (System.nanoTime() - started) / 1e6
      val (layouts, longestMs) = StreamTextDebug.mainLayouts(true)
      // Then what each frame draws.
      val observer = host.viewTreeObserver
      val listener = ViewTreeObserver.OnDrawListener { sample() }
      observer.addOnDrawListener(listener)
      main.postDelayed({
        sample()
        host.viewTreeObserver.removeOnDrawListener(listener)
        val views = JSONObject()
        for ((key, list) in frames) views.put(key, list)
        promise.resolve(
            JSONObject().put("mainMs", mainMs).put("layouts", layouts).put("longestMs", longestMs).put("views", views).toString())
      }, maxOf(ms.toLong(), 32L))
    }
    return promise
  }

  override fun tileMemory(): Promise<String> {
    val promise = Promise<String>()
    main.post {
      // Display lists (StreamTextBlocks) stand for iOS's image tiles.
      val (bytes, blocks, canvases, offWindow) = StreamTextDebug.blockMemory()
      promise.resolve(JSONObject()
          .put("bytes", bytes).put("tiles", blocks).put("canvases", canvases).put("offWindow", offWindow)
          .put("footprintMB", Debug.getPss() / 1024.0)
          .toString())
    }
    return promise
  }

  override fun cover(shown: Boolean): Promise<Double> {
    val promise = Promise<Double>()
    main.post {
      val host = (NitroModules.applicationContext as? ReactContext)?.currentActivity
      if (host == null || host is CoverActivity) {
        promise.reject(IllegalStateException("No app activity to cover"))
        return@post
      }
      val started = System.nanoTime()
      if (shown) {
        host.startActivity(
            Intent(host, CoverActivity::class.java),
            ActivityOptions.makeCustomAnimation(host, 0, 0).toBundle())
      } else {
        CoverActivity.current?.let {
          it.finish()
          @Suppress("DEPRECATION")
          it.overridePendingTransition(0, 0)
        }
      }
      // Resolves once the app's window is hidden, or shown and drawn again.
      val decor = host.window.decorView
      val deadline = SystemClock.uptimeMillis() + 3000
      fun check() {
        val visible = decor.windowVisibility == View.VISIBLE
        if (visible == !shown || SystemClock.uptimeMillis() > deadline) {
          Choreographer.getInstance().postFrameCallback {
            Choreographer.getInstance().postFrameCallback {
              main.post { promise.resolve((System.nanoTime() - started) / 1e6) }
            }
          }
        } else {
          main.postDelayed({ check() }, 16)
        }
      }
      check()
    }
    return promise
  }

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

  override fun selectionGeometry(key: String, location: Double, length: Double): Promise<String> {
    val promise = Promise<String>()
    main.post {
      val view = StreamTextDebug.view(key)
      if (view == null) promise.reject(IllegalStateException("No StreamTextView with nativeID $key and recordDisplayLog"))
      else promise.resolve(view.debugSelectionGeometry(location.toInt(), length.toInt()))
    }
    return promise
  }

  override fun accessibility(key: String, lines: DoubleArray): Promise<String> {
    val promise = Promise<String>()
    main.post {
      val json = StreamTextDebug.accessibility(key, IntArray(lines.size) { lines[it].toInt() })
      if (json == null) promise.reject(IllegalStateException("No StreamTextView with nativeID $key and recordDisplayLog"))
      else promise.resolve(json)
    }
    return promise
  }

  override fun stopSurface(key: String): Promise<String> {
    val promise = Promise<String>()
    main.post {
      val view = StreamTextDebug.view(key)
      val context = view?.context as? ReactContext
      val manager = context?.let { UIManagerHelper.getUIManager(it, UIManagerType.FABRIC) as? FabricUIManager }
      if (view == null || manager == null) {
        promise.reject(IllegalStateException("No Fabric surface for StreamTextView $key"))
        return@post
      }
      val surfaceId = UIManagerHelper.getSurfaceId(view)
      manager.stopSurface(surfaceId)
      promise.resolve(surfaceId.toString())
    }
    return promise
  }

  private fun <T> unsupported(): Promise<T> = Promise.rejected(UnsupportedOperationException("Not on Android yet"))

  private companion object {
    const val TSB_HEADER_SIZE = 64
    val producerQueue = Executors.newSingleThreadScheduledExecutor { Thread(it, "StreamText.testProducer") }
  }
}
