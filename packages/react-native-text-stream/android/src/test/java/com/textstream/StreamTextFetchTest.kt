package com.textstream

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.zip.GZIPOutputStream
import okhttp3.*
import okhttp3.ResponseBody.Companion.toResponseBody
import okio.Buffer
import okio.Source
import okio.Timeout
import okio.buffer
import org.junit.Assert.*
import org.junit.Test

class StreamTextFetchTest {
  private class Events : StreamTextFetch.Events {
    val ended = CountDownLatch(1)
    var status = 0
    var completions = 0
    var bytes = 0L
    var error: String? = null
    var onPublish: (() -> Unit)? = null
    var consumeStatus = StreamTextFetch.STATUS_CONTINUE
    var consumedBytes = 0
    var onConsume: (() -> Unit)? = null
    override fun response(handle: Long, status: Int, headers: Array<String>, url: String) {
      check(completions == 0) { "response after native handle release" }
    }
    override fun publish(handle: Long, committed: Long) {
      check(completions == 0) { "publication after native handle release" }
      bytes = committed
      onPublish?.invoke()
    }
    override fun consume(handle: Long, bytes: ByteArray, count: Int): Int {
      check(completions == 0) { "parser consumed after native handle release" }
      consumedBytes += count
      onConsume?.invoke()
      return consumeStatus
    }
    override fun complete(handle: Long, status: Int, error: String?) {
      check(completions++ == 0) { "duplicate native completion" }
      this.status = status
      this.error = error
      ended.countDown()
    }
  }

  private val request = Request.Builder().url("http://localhost/stream").build()
  private fun response(body: ResponseBody): Response = Response.Builder().request(request)
      .protocol(Protocol.HTTP_1_1).code(200).message("OK").body(body).build()

  @Test fun cancelWhileQueuedCompletesBeforeAnotherRequestFinishes() = queued(cancel = true)
  @Test fun initialDeadlineIncludesDispatcherQueue() = queued(cancel = false)

  private fun queued(cancel: Boolean) {
    val entered = CountDownLatch(1)
    val release = CountDownLatch(1)
    val dispatcher = Dispatcher().apply { maxRequestsPerHost = 1 }
    val client = OkHttpClient.Builder().dispatcher(dispatcher).addInterceptor {
      entered.countDown()
      check(release.await(5, TimeUnit.SECONDS))
      response("ok".toResponseBody())
    }.build()
    val occupying = client.newCall(request)
    val occupiedDone = CountDownLatch(1)
    occupying.enqueue(object : Callback {
      override fun onFailure(call: Call, e: IOException) { occupiedDone.countDown() }
      override fun onResponse(call: Call, response: Response) { response.close(); occupiedDone.countDown() }
    })
    try {
      assertTrue(entered.await(2, TimeUnit.SECONDS))
      val events = Events()
      val fetch = StreamTextFetch(1, ByteBuffer.allocateDirect(16), events)
      fetch.enqueue(client, request, if (cancel) 10000 else 100)
      assertEquals(1, dispatcher.queuedCallsCount())
      if (cancel) fetch.cancel()
      assertTrue(events.ended.await(2, TimeUnit.SECONDS))
      assertEquals(if (cancel) 3 else 2, events.status)
      assertEquals(0L, events.bytes)
      assertEquals(1L, release.count)
      release.countDown()
      assertTrue(occupiedDone.await(2, TimeUnit.SECONDS))
      dispatcher.executorService.shutdown()
      assertTrue(dispatcher.executorService.awaitTermination(2, TimeUnit.SECONDS))
      assertEquals(1, events.completions)
    } finally {
      release.countDown()
      occupying.cancel()
      dispatcher.executorService.shutdownNow()
      client.connectionPool.evictAll()
    }
  }

  @Test fun cancelBufferedBodyStopsPublishingAndIgnoresLateCallbacks() {
    val events = Events()
    val data = ByteBuffer.allocateDirect(100000)
    val fetch = StreamTextFetch(1, data, events)
    events.onPublish = { fetch.cancel() }
    val call = OkHttpClient().newCall(request)
    fetch.onResponse(call, response(ByteArray(100000) { 65 }.toResponseBody()))
    assertEquals(3, events.status)
    assertEquals(8192L, events.bytes)
    val position = data.position()
    fetch.onResponse(call, response("late".toResponseBody()))
    fetch.onFailure(call, IOException("late"))
    fetch.cancel()
    assertEquals(position, data.position())
    assertEquals(1, events.completions)
  }

  @Test fun capacityEofAndTerminalStateMatrix() {
    val client = OkHttpClient()
    for (capacity in 1..8) for (length in 0..12) {
      val call = client.newCall(request)
      val events = Events()
      val data = ByteBuffer.allocateDirect(capacity)
      val fetch = StreamTextFetch(1, data, events)
      fetch.onResponse(call, response(ByteArray(length) { 65 }.toResponseBody()))
      assertEquals(if (length > capacity) 4 else 1, events.status)
      assertEquals(minOf(capacity, length).toLong(), events.bytes)
      fetch.cancel()
      fetch.onResponse(call, response("late".toResponseBody()))
      assertEquals(1, events.completions)
      assertEquals(minOf(capacity, length), data.position())
    }
  }

  @Test fun bodyIsUtf8AndExplicitEncodingIsPreserved() {
    val text = "é Привет 🚀\u0000"
    val request = StreamTextFetch.request("http://localhost/", "POST",
        arrayOf("Content-Type", "text/plain; charset=iso-8859-1", "Accept-Encoding", "identity"), text.toByteArray(Charsets.UTF_8))
    val body = Buffer()
    request.body!!.writeTo(body)
    assertArrayEquals(text.toByteArray(Charsets.UTF_8), body.readByteArray())
    assertEquals("identity", request.header("Accept-Encoding"))
  }

  @Test fun explicitGzipIsDecodedAndCapacityAppliesToDecodedBytes() {
    val compressed = ByteArrayOutputStream()
    GZIPOutputStream(compressed).use { it.write("hello world".toByteArray()) }
    for (capacity in listOf(5, 11, 20)) {
      val data = ByteBuffer.allocateDirect(capacity)
      val events = Events()
      val fetch = StreamTextFetch(1, data, events)
      val call = OkHttpClient().newCall(request)
      fetch.onResponse(call, response(compressed.toByteArray().toResponseBody()).newBuilder().header("Content-Encoding", "gzip").build())
      assertEquals(if (capacity < 11) 4 else 1, events.status)
      data.flip()
      val bytes = ByteArray(data.remaining()); data.get(bytes)
      assertEquals("hello world".take(capacity), String(bytes))
    }
  }

  @Test fun partialReadFailureKeepsPublishedPrefixAndCompletesOnce() {
    val events = Events()
    val data = ByteBuffer.allocateDirect(100)
    var sent = false
    val body = object : ResponseBody() {
      override fun contentType(): MediaType? = null
      override fun contentLength() = -1L
      private val input = object : Source {
        override fun timeout() = Timeout.NONE
        override fun close() {}
        override fun read(sink: Buffer, byteCount: Long): Long {
          if (sent) throw IOException("broken body")
          sent = true; sink.writeUtf8("abc"); return 3
        }
      }.buffer()
      override fun source() = input
    }
    StreamTextFetch(1, data, events).onResponse(OkHttpClient().newCall(request), response(body))
    assertEquals(2, events.status)
    assertEquals(3L, events.bytes)
    assertEquals("broken body", events.error)
    assertEquals(1, events.completions)
  }

  @Test fun parsedTransportUsesPrivateScratchAndDoesNotSpendOutputCapacityOnMetadata() {
    val events = Events()
    val data = ByteBuffer.allocateDirect(2)
    val fetch = StreamTextFetch(1, data, events, parsed = true)
    fetch.onResponse(OkHttpClient().newCall(request), response("event: metadata\ndata: visible\n\n".toResponseBody()))
    assertEquals(1, events.status)
    assertEquals("event: metadata\ndata: visible\n\n".toByteArray().size, events.consumedBytes)
    // Only C++ parser output can advance this buffer; wire metadata never does.
    assertEquals(0, data.position())
    assertEquals(0L, events.bytes)
  }

  @Test fun parsedProtocolDoneCancelsTheNetworkAfterTheCurrentRead() {
    val events = Events().also { it.consumeStatus = StreamTextFetch.STATUS_DONE }
    val data = ByteBuffer.allocateDirect(1)
    val call = OkHttpClient().newCall(request)
    StreamTextFetch(1, data, events, parsed = true).onResponse(call, response(ByteArray(9000) { 65 }.toResponseBody()))
    assertEquals(1, events.status)
    assertTrue(call.isCanceled())
    assertEquals(8192, events.consumedBytes)
    assertEquals(0, data.position())
  }

  @Test fun parsedTerminalErrorsStopBeforeTheNextNetworkRead() {
    for ((parserStatus, expectedStatus) in listOf(
        StreamTextFetch.STATUS_ERROR to 2,
        StreamTextFetch.STATUS_OVERFLOW to 4,
    )) {
      val events = Events().also { it.consumeStatus = parserStatus }
      val data = ByteBuffer.allocateDirect(1)
      val call = OkHttpClient().newCall(request)
      StreamTextFetch(1, data, events, parsed = true).onResponse(call, response(ByteArray(9000) { 65 }.toResponseBody()))
      assertEquals(expectedStatus, events.status)
      assertEquals(1, events.completions)
      assertTrue(call.isCanceled())
      assertEquals(8192, events.consumedBytes)
      assertEquals(0, data.position())
    }
  }

  @Test fun parsedCancellationStopsAfterTheCurrentParserCallback() {
    val events = Events()
    val data = ByteBuffer.allocateDirect(1)
    val fetch = StreamTextFetch(1, data, events, parsed = true)
    events.onConsume = { fetch.cancel() }
    fetch.onResponse(OkHttpClient().newCall(request), response(ByteArray(9000) { 65 }.toResponseBody()))
    assertEquals(3, events.status)
    assertEquals(1, events.completions)
    assertEquals(8192, events.consumedBytes)
    assertEquals(0, data.position())
  }
}
