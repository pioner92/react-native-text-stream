package com.textstream

import com.facebook.proguard.annotations.DoNotStrip
import com.facebook.react.modules.network.OkHttpClientProvider
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import okhttp3.Call
import okhttp3.Callback
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response

/**
 * streamFetch() on Android (cpp/StreamTextFetch.hpp): OkHttp reads the response body straight into
 * the TSB1 buffer's data (`data`, a ByteBuffer over it) on its callback thread, the buffer's only
 * writer, and publishes each read. React Native's own client: the app's cookies and settings apply.
 */
@DoNotStrip
internal class StreamTextFetch private constructor(private val handle: Long, private val data: ByteBuffer) : Callback {
  @Volatile private var call: Call? = null
  @Volatile private var cancelled = false
  private val completed = AtomicBoolean(false)

  @DoNotStrip
  fun cancel() {
    cancelled = true
    call?.cancel()
  }

  override fun onFailure(call: Call, e: IOException) {
    complete(if (cancelled) STATUS_CANCELLED else STATUS_ERROR, e.message ?: e.toString())
  }

  override fun onResponse(call: Call, response: Response) {
    response.use {
      val names = response.headers.names()
      val headers = ArrayList<String>(names.size * 2)
      for (name in names) {
        headers.add(name.lowercase())
        headers.add(response.headers.values(name).joinToString(", "))
      }
      nativeResponse(handle, response.code, headers.toTypedArray(), response.request.url.toString())
      val source = response.body?.source()
      if (source == null) {
        complete(STATUS_DONE, null)
        return
      }
      val capacity = data.capacity()
      var committed = 0
      try {
        while (true) {
          if (committed == capacity) {
            if (source.exhausted()) {
              complete(STATUS_DONE, null)
            } else {
              // The rest doesn't fit: the status says so, and the download stops.
              complete(STATUS_OVERFLOW, null)
              call.cancel()
            }
            return
          }
          data.limit(capacity)
          data.position(committed)
          val read = source.read(data)
          if (read == -1) {
            complete(STATUS_DONE, null)
            return
          }
          committed += read
          nativePublish(handle, committed.toLong())
        }
      } catch (e: IOException) {
        complete(if (cancelled) STATUS_CANCELLED else STATUS_ERROR, e.message ?: e.toString())
      }
    }
  }

  /** Exactly once: the native side sets the buffer's status, tells JS, and frees the handle. */
  private fun complete(status: Int, error: String?) {
    // A reason only for a failure, as on iOS: a cancelled request has none.
    if (completed.compareAndSet(false, true)) nativeComplete(handle, status, if (status == STATUS_ERROR) error else null)
  }

  companion object {
    private const val STATUS_DONE = 1
    private const val STATUS_ERROR = 2
    private const val STATUS_CANCELLED = 3
    private const val STATUS_OVERFLOW = 4

    private val client: OkHttpClient by lazy { OkHttpClientProvider.getOkHttpClient() }

    /** From the JS thread. `headers`: `[name, value, …]`; `timeoutMs`: the longest wait for bytes. */
    @JvmStatic
    @DoNotStrip
    fun start(
        handle: Long,
        url: String,
        method: String,
        headers: Array<String>,
        body: String?,
        timeoutMs: Long,
        data: ByteBuffer,
    ): StreamTextFetch {
      val fetch = StreamTextFetch(handle, data)
      val request = try {
        val builder = Request.Builder().url(url)
        var contentType: String? = null
        for (i in 0 until headers.size / 2) {
          val name = headers[i * 2]
          val value = headers[i * 2 + 1]
          // OkHttp decompresses the body only when it asked for the encoding itself.
          if (name.equals("Accept-Encoding", ignoreCase = true)) continue
          if (name.equals("Content-Type", ignoreCase = true)) contentType = value
          builder.addHeader(name, value)
        }
        val upper = method.uppercase()
        val requestBody = when {
          body != null -> body.toRequestBody(contentType?.toMediaTypeOrNull())
          upper == "POST" || upper == "PUT" || upper == "PATCH" -> ByteArray(0).toRequestBody(null)
          else -> null
        }
        builder.method(upper, requestBody).build()
      } catch (e: IllegalArgumentException) {
        fetch.complete(STATUS_ERROR, e.message ?: "invalid request")
        return fetch
      }
      val callClient = if (timeoutMs > 0) client.newBuilder().readTimeout(timeoutMs, TimeUnit.MILLISECONDS).build() else client
      val call = callClient.newCall(request)
      fetch.call = call
      call.enqueue(fetch)
      return fetch
    }

    @JvmStatic private external fun nativeResponse(handle: Long, status: Int, headers: Array<String>, url: String)

    @JvmStatic private external fun nativePublish(handle: Long, committed: Long)

    @JvmStatic private external fun nativeComplete(handle: Long, status: Int, error: String?)
  }
}
