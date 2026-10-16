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
    private val parse: (String, Boolean, Boolean) -> StreamTextMarkdown? = { text, github, final -> StreamTextNative.parseMarkdownStreaming(text, github, final) },
    private val style: (StreamTextMarkdown, Int, Int, StreamTextStyle) -> CharSequence = { document, color, width, textStyle -> document.styled(color, width, textStyle) },
    private val deliver: (Long, Long, Boolean, StreamTextMarkdown, CharSequence, Int, () -> Unit) -> Unit,
) {
  private var pendingText: String? = null
  private var pendingGeneration = 0L
  private var pendingGithub = false
  private var pendingColor = 0
  private var pendingBytes = 0L
  private var pendingWidth = 0
  private var pendingStyle = StreamTextStyle()
  private var pendingFinal = false
  private var running = false
  private var acknowledged: StreamTextMarkdown? = null
  private var acknowledgedWidth = -1
  // A pending image URL can grow for many stream snapshots while its visible U+FFFC attachment
  // and all spans stay identical. Keep the last worker-built text for that exact visual state so
  // table cell StaticLayouts and image spans are not rebuilt for invisible bytes.
  private var acknowledgedStyled: CharSequence? = null
  private var acknowledgedColor = 0
  private var acknowledgedStyle = StreamTextStyle()

  fun submit(text: String, generation: Long, bytes: Long, github: Boolean, color: Int, width: Int = 1, textStyle: StreamTextStyle = StreamTextStyle(), final: Boolean = false) {
    synchronized(this) {
      pendingText = text
      pendingGeneration = generation
      pendingGithub = github
      pendingColor = color
      pendingBytes = bytes
      pendingWidth = width
      pendingStyle = textStyle
      pendingFinal = final
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
      acknowledgedStyled = null
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
      val final: Boolean
      val base: StreamTextMarkdown?
      val baseWidth: Int
      val baseStyled: CharSequence?
      val baseColor: Int
      val baseStyle: StreamTextStyle
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
        final = pendingFinal
        base = acknowledged
        baseWidth = acknowledgedWidth
        baseStyled = acknowledgedStyled
        baseColor = acknowledgedColor
        baseStyle = acknowledgedStyle
        pendingText = null
      }
      val result = parse(input, github, final) ?: StreamTextMarkdown.plain(input)
      // Span construction is linear in run count and stays off the UI thread.
      val changedStart = if (result.hasRichBlocks && width != baseWidth) 0 else result.changedStartFrom(base)
      // `changedStart == text.length` means the parsed visual document is exactly unchanged.
      // Reusing is deliberately narrow: text style, color and measured attachment width must
      // also match, and the old snapshot must have been acknowledged by the UI.
      val styled = if (baseStyled != null && base != null &&
          changedStart == result.text.length && result.text.length == base.text.length &&
          width == baseWidth && color == baseColor && textStyle == baseStyle) baseStyled
      else style(result, color, width, textStyle)
      deliver(generation, bytes, final, result, styled, changedStart) {
        synchronized(this) {
          if (pendingGeneration == generation) {
            acknowledged = result
            acknowledgedWidth = width
            acknowledgedStyled = styled
            acknowledgedColor = color
            acknowledgedStyle = textStyle
          }
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
