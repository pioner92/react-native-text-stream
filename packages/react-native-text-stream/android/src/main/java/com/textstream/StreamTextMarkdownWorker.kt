package com.textstream

import java.util.concurrent.Executor
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Coalesces stream updates before MD4C runs. Parsing never happens on the UI thread; while one
 * parse is running all incoming bytes replace the pending snapshot, so a burst costs one extra
 * parse instead of one parse per network chunk.
 */
internal class StreamTextMarkdownWorker(
    private val executor: Executor = sharedExecutor,
    private val parse: (String, Boolean) -> StreamTextMarkdown? = { text, github -> StreamTextNative.parseMarkdown(text, github) },
    private val style: (StreamTextMarkdown, Int) -> CharSequence = { document, color -> document.styled(color) },
    private val deliver: (Long, Long, StreamTextMarkdown, CharSequence, Int, () -> Unit) -> Unit,
) {
  private var pendingText: String? = null
  private var pendingGeneration = 0L
  private var pendingGithub = false
  private var pendingColor = 0
  private var pendingBytes = 0L
  private var running = false
  private var acknowledged: StreamTextMarkdown? = null

  fun submit(text: String, generation: Long, bytes: Long, github: Boolean, color: Int) {
    synchronized(this) {
      pendingText = text
      pendingGeneration = generation
      pendingGithub = github
      pendingColor = color
      pendingBytes = bytes
      if (running) return
      running = true
    }
    executor.execute { drain() }
  }

  fun clear() {
    synchronized(this) {
      pendingText = null
      pendingGeneration++
      acknowledged = null
    }
  }

  private fun drain() {
    while (true) {
      val input: String
      val generation: Long
      val github: Boolean
      val color: Int
      val bytes: Long
      val base: StreamTextMarkdown?
      synchronized(this) {
        input = pendingText ?: run {
          running = false
          return
        }
        generation = pendingGeneration
        github = pendingGithub
        color = pendingColor
        bytes = pendingBytes
        base = acknowledged
        pendingText = null
      }
      val result = parse(input, github) ?: StreamTextMarkdown.plain(input)
      // Span construction is linear in run count and stays off the UI thread.
      deliver(generation, bytes, result, style(result, color), result.changedStartFrom(base)) {
        synchronized(this) { if (pendingGeneration == generation) acknowledged = result }
        executor.execute { drain() }
      }
      return
    }
  }

  companion object {
    private val sharedExecutor: ExecutorService = Executors.newFixedThreadPool(2) { runnable ->
      Thread(runnable, "StreamTextMarkdown").apply { isDaemon = true }
    }
  }
}
