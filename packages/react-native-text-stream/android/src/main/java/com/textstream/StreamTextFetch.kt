package com.textstream

import com.facebook.proguard.annotations.DoNotStrip
import com.facebook.react.modules.network.OkHttpClientProvider
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit
import okhttp3.Call
import okhttp3.Callback
import okhttp3.Dispatcher
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import okio.GzipSource
import okio.buffer

/** Network reads use private storage. The lock guards every access to the native handle/data,
 * so cancellation can finish even a queued call and free its buffer before a late callback arrives. */
@DoNotStrip
internal class StreamTextFetch internal constructor(
    private val handle: Long,
    private val data: ByteBuffer,
    private val events: Events = JniEvents,
    /** Parsed transports retain no wire body in [data]; native owns their output buffer. */
    private val parsed: Boolean = false,
) : Callback {
  internal interface Events {
    fun response(handle: Long, status: Int, headers: Array<String>, url: String)
    fun publish(handle: Long, committed: Long)
    /** Parser-only path; raw text never calls this or allocates parser scratch state. */
    fun consume(handle: Long, bytes: ByteArray, count: Int): Int = STATUS_CONTINUE
    fun complete(handle: Long, status: Int, error: String?)
  }

  private val lock = Any()
  private var call: Call? = null
  private var completed = false
  private var firstResponseDeadline: ScheduledFuture<*>? = null

  @DoNotStrip
  fun cancel() = stop(STATUS_CANCELLED, null)

  private fun stop(status: Int, error: String?) {
    val stopped = synchronized(lock) {
      if (completed) return
      finishLocked(status, error)
      call
    }
    stopped?.cancel()
  }

  override fun onFailure(call: Call, e: IOException) {
    synchronized(lock) {
      finishLocked(if (call.isCanceled()) STATUS_CANCELLED else STATUS_ERROR, e.message ?: e.toString())
    }
  }

  override fun onResponse(call: Call, response: Response) {
    try {
      response.use {
        synchronized(lock) {
          if (completed) return
          firstResponseDeadline?.cancel(false)
          firstResponseDeadline = null
          if (call.isCanceled()) { finishLocked(STATUS_CANCELLED, null); return }
          val headers = ArrayList<String>()
          for (name in response.headers.names()) {
            headers.add(name.lowercase())
            headers.add(response.headers.values(name).joinToString(", "))
          }
          events.response(handle, response.code, headers.toTypedArray(), response.request.url.toString())
          if (completed) return
        }
        val rejectParsedResponse = parsed && !response.isSuccessful
        if (rejectParsedResponse) {
          synchronized(lock) {
            if (!completed) finishLocked(STATUS_ERROR, "streamFetch: HTTP ${response.code}")
          }
          call.cancel()
          return
        }
        val body = response.body
        if (body == null) { synchronized(lock) { finishLocked(STATUS_DONE, null) }; return }
        // OkHttp handles implicit gzip itself. Explicit Accept-Encoding must also decode gzip,
        // without silently changing the header the caller asked to send.
        val source = if (response.header("Content-Encoding").equals("gzip", ignoreCase = true))
          GzipSource(body.source()).buffer() else body.source()
        source.use {
          val scratch = ByteArray(8192)
          var committed = 0
          val capacity = data.capacity()
          while (true) {
            synchronized(lock) { if (completed) return }
            if (!parsed && committed == capacity) {
              val exhausted = source.exhausted()
              synchronized(lock) {
                if (completed) return
                finishLocked(if (call.isCanceled()) STATUS_CANCELLED else if (exhausted) STATUS_DONE else STATUS_OVERFLOW, null)
              }
              if (!exhausted) call.cancel()
              return
            }
            // Never block holding the lifecycle lock, and never let Okio write into released memory.
            val read = source.read(scratch, 0, if (parsed) scratch.size else minOf(scratch.size, capacity - committed))
            var cancelAfter = false
            synchronized(lock) {
              if (completed) return
              if (call.isCanceled()) { finishLocked(STATUS_CANCELLED, null); return }
              if (read == -1) { finishLocked(STATUS_DONE, null); return }
              if (parsed) {
                when (events.consume(handle, scratch, read)) {
                  STATUS_DONE -> { finishLocked(STATUS_DONE, null); cancelAfter = true }
                  STATUS_ERROR -> { finishLocked(STATUS_ERROR, null); cancelAfter = true }
                  STATUS_OVERFLOW -> { finishLocked(STATUS_OVERFLOW, null); cancelAfter = true }
                }
              } else {
                data.position(committed)
                data.put(scratch, 0, read)
                committed += read
                events.publish(handle, committed.toLong())
              }
            }
            if (cancelAfter) { call.cancel(); return }
          }
        }
      }
    } catch (e: Exception) {
      synchronized(lock) {
        finishLocked(if (call.isCanceled()) STATUS_CANCELLED else STATUS_ERROR, e.message ?: e.toString())
      }
    }
  }

  /** Only under lock. No path may touch the handle or native bytes after this point. */
  private fun finishLocked(status: Int, error: String?) {
    if (completed) return
    completed = true
    firstResponseDeadline?.cancel(false)
    firstResponseDeadline = null
    events.complete(handle, status, if (status == STATUS_ERROR) error else null)
  }

  internal fun enqueue(client: OkHttpClient, request: Request, timeoutMs: Long) {
    try {
      val configured = client.newBuilder()
          .connectTimeout(timeoutMs, TimeUnit.MILLISECONDS)
          .writeTimeout(timeoutMs, TimeUnit.MILLISECONDS)
          .readTimeout(timeoutMs, TimeUnit.MILLISECONDS)
          .build()
      synchronized(lock) {
        if (completed) return
        val started = configured.newCall(request)
        call = started
        // Includes dispatcher queueing, DNS, connection, upload and response headers.
        firstResponseDeadline = deadlines.schedule({ stop(STATUS_ERROR, "streamFetch: response timeout") }, timeoutMs, TimeUnit.MILLISECONDS)
        started.enqueue(this)
      }
    } catch (e: Exception) {
      stop(STATUS_ERROR, e.message ?: "invalid request")
    }
  }

  companion object {
    internal const val STATUS_CONTINUE = 0
    internal const val STATUS_DONE = 1
    internal const val STATUS_ERROR = 2
    internal const val STATUS_CANCELLED = 3
    internal const val STATUS_OVERFLOW = 4

    private val deadlines = ScheduledThreadPoolExecutor(1) { runnable ->
      Thread(runnable, "StreamText.deadlines").apply { isDaemon = true }
    }.apply { removeOnCancelPolicy = true }

    // Share the app's cookies, interceptors and connection pool, but don't occupy its request slots.
    // Not 5 per host: long-lived streams to one API would queue, and queueing counts toward the deadline.
    private val client: OkHttpClient by lazy {
      val app = OkHttpClientProvider.getOkHttpClient()
      app.newBuilder().dispatcher(Dispatcher().apply {
        maxRequests = app.dispatcher.maxRequests
        maxRequestsPerHost = app.dispatcher.maxRequests
      }).build()
    }

    internal fun request(url: String, method: String, headers: Array<String>, body: ByteArray?): Request {
      val builder = Request.Builder().url(url)
      var contentType: String? = null
      for (i in 0 until headers.size / 2) {
        val name = headers[i * 2]
        val value = headers[i * 2 + 1]
        if (name.equals("Content-Type", ignoreCase = true)) contentType = value
        builder.addHeader(name, value)
      }
      val upper = method.uppercase()
      val requestBody = when {
        // The string contract is UTF-8 on both platforms, independent of the declared charset.
        body != null -> body.toRequestBody(contentType?.toMediaTypeOrNull())
        upper == "POST" || upper == "PUT" || upper == "PATCH" -> ByteArray(0).toRequestBody(null)
        else -> null
      }
      return builder.method(upper, requestBody).build()
    }

    @JvmStatic
    @DoNotStrip
    fun start(handle: Long, url: String, method: String, headers: Array<String>, body: ByteArray?, timeoutMs: Long, data: ByteBuffer, parsed: Boolean): StreamTextFetch {
      val fetch = StreamTextFetch(handle, data, parsed = parsed)
      try {
        require(timeoutMs in 1..Int.MAX_VALUE.toLong()) { "invalid timeout" }
        fetch.enqueue(client, request(url, method, headers, body), timeoutMs)
      } catch (e: Exception) {
        fetch.stop(STATUS_ERROR, e.message ?: "invalid request")
      }
      return fetch
    }

    private object JniEvents : Events {
      override fun response(handle: Long, status: Int, headers: Array<String>, url: String) = nativeResponse(handle, status, headers, url)
      override fun publish(handle: Long, committed: Long) = nativePublish(handle, committed)
      override fun consume(handle: Long, bytes: ByteArray, count: Int) = nativeConsume(handle, bytes, count)
      override fun complete(handle: Long, status: Int, error: String?) = nativeComplete(handle, status, error)
    }

    @JvmStatic private external fun nativeResponse(handle: Long, status: Int, headers: Array<String>, url: String)
    @JvmStatic private external fun nativePublish(handle: Long, committed: Long)
    @JvmStatic private external fun nativeConsume(handle: Long, bytes: ByteArray, count: Int): Int
    @JvmStatic private external fun nativeComplete(handle: Long, status: Int, error: String?)
  }
}
