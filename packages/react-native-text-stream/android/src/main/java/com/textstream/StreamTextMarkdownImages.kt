package com.textstream

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Handler
import android.os.Looper
import java.io.ByteArrayOutputStream
import java.util.LinkedHashMap
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import okhttp3.Authenticator
import okhttp3.Call
import okhttp3.CookieJar
import okhttp3.OkHttpClient
import okhttp3.Request

/** A process-wide bounded loader. Cache and in-flight ownership share one lock; callbacks never
 * retain a view beyond its subscription, and cancellation cannot remove a newer job for the URL. */
internal object StreamTextMarkdownImages {
  internal sealed class Result {
    data class Loaded(val bitmap: Bitmap) : Result()
    data object Blocked : Result()
    data object Oversize : Result()
    data object Failed : Result()
  }
  private const val MAX_DOWNLOAD = 8 * 1024 * 1024
  private const val MAX_EDGE = 2048
  private const val MAX_PIXELS = 4_000_000
  private const val CACHE_BYTES = 32 * 1024 * 1024
  private val lock = Any()
  private val main = Handler(Looper.getMainLooper())
  private val pool = ThreadPoolExecutor(2, 2, 0, TimeUnit.MILLISECONDS, LinkedBlockingQueue<Runnable>()) {
    Thread(it, "StreamTextMarkdownImage").apply { isDaemon = true }
  }
  private val client = OkHttpClient.Builder()
    .cookieJar(CookieJar.NO_COOKIES).authenticator(Authenticator.NONE).proxyAuthenticator(Authenticator.NONE)
    .followRedirects(false).followSslRedirects(false)
    .connectTimeout(10, TimeUnit.SECONDS).readTimeout(15, TimeUnit.SECONDS).callTimeout(30, TimeUnit.SECONDS).build()
  private val cache = LinkedHashMap<String, Bitmap>(16, 0.75f, true)
  private var cacheBytes = 0
  private val pending = HashMap<String, Job>()
  private class Subscriber(val deliver: (Result) -> Unit) { val cancelled = AtomicBoolean() }
  private class Job(val url: String) : Runnable {
    val subscribers = LinkedHashSet<Subscriber>()
    var call: Call? = null // protected by lock
    override fun run() {
      val result = try { decode(this) } catch (_: Exception) { Result.Failed }
      val targets = synchronized(lock) {
        if (pending[url] !== this) return
        if (result is Result.Loaded) {
          cache.remove(url)?.let { cacheBytes -= it.allocationByteCount }
          cache[url] = result.bitmap
          cacheBytes += result.bitmap.allocationByteCount
          val iterator = cache.entries.iterator()
          while (cacheBytes > CACHE_BYTES && iterator.hasNext()) {
            cacheBytes -= iterator.next().value.allocationByteCount
            iterator.remove() // Visible spans retain their bitmap; never recycle a shared bitmap.
          }
        }
        pending.remove(url)
        subscribers.toList().also { subscribers.clear() }
      }
      main.post { targets.forEach { if (!it.cancelled.get()) it.deliver(result) } }
    }
  }

  fun load(url: String, deliver: (Result) -> Unit): () -> Unit {
    val subscriber = Subscriber(deliver)
    synchronized(lock) {
      val cached = cache[url]
      if (cached != null) main.post { if (!subscriber.cancelled.get()) deliver(Result.Loaded(cached)) }
      else {
        val existing = pending[url]
        if (existing != null) existing.subscribers.add(subscriber)
        else {
          val job = Job(url)
          job.subscribers.add(subscriber)
          pending[url] = job
          pool.execute(job)
        }
      }
    }
    return {
      subscriber.cancelled.set(true)
      synchronized(lock) {
        pending[url]?.let { job ->
          job.subscribers.remove(subscriber)
          if (job.subscribers.isEmpty()) {
            pending.remove(url)
            pool.remove(job)
            job.call?.cancel()
          }
        }
      }
    }
  }

  private fun decode(job: Job): Result {
    val request = try { Request.Builder().url(job.url).build() } catch (_: IllegalArgumentException) { return Result.Blocked }
    val call = client.newCall(request)
    synchronized(lock) {
      if (pending[job.url] !== job || job.subscribers.isEmpty()) return Result.Failed
      job.call = call
    }
    return call.execute().use { response ->
      if (!response.isSuccessful) return Result.Failed
      val body = response.body ?: return Result.Failed
      if (body.contentLength() > MAX_DOWNLOAD) return Result.Oversize
      val bytes = body.byteStream().use { input ->
        val out = ByteArrayOutputStream(minOf(MAX_DOWNLOAD, maxOf(32 * 1024, body.contentLength().toInt())))
        val chunk = ByteArray(8192)
        while (true) {
          val count = input.read(chunk)
          if (count < 0) break
          if (out.size() > MAX_DOWNLOAD - count) return Result.Oversize
          out.write(chunk, 0, count)
        }
        out.toByteArray()
      }
      if (call.isCanceled()) return Result.Failed
      val bounds = BitmapFactory.Options().also { it.inJustDecodeBounds = true }
      BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
      if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return Result.Failed
      var sample = 1
      while (bounds.outWidth / sample > MAX_EDGE || bounds.outHeight / sample > MAX_EDGE ||
          (bounds.outWidth.toLong() / sample) * (bounds.outHeight.toLong() / sample) > MAX_PIXELS) sample *= 2
      val options = BitmapFactory.Options().also { it.inSampleSize = sample; it.inPreferredConfig = Bitmap.Config.ARGB_8888 }
      val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options) ?: return Result.Failed
      if (bitmap.width.toLong() * bitmap.height > MAX_PIXELS || bitmap.width > MAX_EDGE || bitmap.height > MAX_EDGE) {
        bitmap.recycle(); return Result.Oversize
      }
      Result.Loaded(bitmap)
    }
  }
}
