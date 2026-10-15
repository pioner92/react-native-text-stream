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
    private val style: (StreamTextMarkdown, Int, Int, StreamTextStyle) -> CharSequence = { document, color, width, textStyle -> document.styled(color, width, textStyle) },
    private val deliver: (Long, Long, StreamTextMarkdown, CharSequence, Int, () -> Unit) -> Unit,
) {
  private var pendingText: String? = null
  private var pendingGeneration = 0L
  private var pendingGithub = false
  private var pendingColor = 0
  private var pendingBytes = 0L
  private var pendingWidth = 0
  private var pendingStyle = StreamTextStyle()
  private var running = false
  private var acknowledged: StreamTextMarkdown? = null
  private var acknowledgedWidth = -1

  fun submit(text: String, generation: Long, bytes: Long, github: Boolean, color: Int, width: Int = 1, textStyle: StreamTextStyle = StreamTextStyle()) {
    synchronized(this) {
      pendingText = text
      pendingGeneration = generation
      pendingGithub = github
      pendingColor = color
      pendingBytes = bytes
      pendingWidth = width
      pendingStyle = textStyle
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
      acknowledgedWidth = -1
    }
  }

  private fun drain() {
    while (true) {
      val input: String
      val generation: Long
      val github: Boolean
      val color: Int
      val bytes: Long
      val width: Int
      val textStyle: StreamTextStyle
      val base: StreamTextMarkdown?
      val baseWidth: Int
      synchronized(this) {
        input = pendingText ?: run {
          running = false
          return
        }
        generation = pendingGeneration
        github = pendingGithub
        color = pendingColor
        bytes = pendingBytes
        width = pendingWidth
        textStyle = pendingStyle
        base = acknowledged
        baseWidth = acknowledgedWidth
        pendingText = null
      }
      val result = parse(input, github) ?: StreamTextMarkdown.plain(input)
      // Span construction is linear in run count and stays off the UI thread.
      val changedStart = if (result.hasRichBlocks && width != baseWidth) 0 else result.changedStartFrom(base)
      deliver(generation, bytes, result, style(result, color, width, textStyle), changedStart) {
        synchronized(this) {
          if (pendingGeneration == generation) { acknowledged = result; acknowledgedWidth = width }
        }
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
