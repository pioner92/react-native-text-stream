package com.textstream

import android.graphics.Canvas
import android.graphics.RenderNode
import android.text.Layout
import androidx.annotation.RequiresApi
import kotlin.math.max
import kotlin.math.min

/**
 * The text in blocks of [LINES] lines, each its own display list, recorded only near the screen:
 * the part on screen plus [AHEAD_PX] above and below, as iOS draws tiles. Blocks farther than
 * [KEEP_PX] are dropped. An append re-records only the blocks it touched; a long text mounted far
 * below the screen records nothing until it scrolls near.
 */
@RequiresApi(29)
internal class StreamTextBlocks {
  private val nodes = ArrayList<RenderNode>()
  /** Recorded and current, per block. */
  private var recorded = BooleanArray(0)
  /** Blocks from this one on changed since they were recorded. */
  private var dirtyFrom = 0
  /** The blocks the last draw drew: a scroll that needs others redraws. */
  var drawnFirst = 0
    private set
  var drawnLast = -1
    private set

  /** Display lists held. */
  val count: Int
    get() = nodes.indices.count { it < recorded.size && recorded[it] && nodes[it].hasDisplayList() }

  /** Bytes the display lists held take, about. */
  val bytes: Long
    get() = nodes.indices.sumOf {
      if (it < recorded.size && recorded[it] && nodes[it].hasDisplayList()) nodes[it].computeApproximateMemoryUsage() else 0L
    }

  /** Drops the display lists of the blocks outside `keep`. */
  fun keepOnly(keep: IntRange) {
    for (i in recorded.indices) {
      if (recorded[i] && i !in keep) {
        nodes[i].discardDisplayList()
        recorded[i] = false
      }
    }
  }

  fun markDirty(block: Int) {
    dirtyFrom = min(dirtyFrom, max(block, 0))
  }

  fun discard() {
    for (node in nodes) node.discardDisplayList()
    nodes.clear()
    recorded = BooleanArray(0)
    dirtyFrom = 0
    drawnFirst = 0
    drawnLast = -1
  }

  /**
   * Draws the blocks in `draw` of the first `lineCount` lines of `layout`, recording those not
   * current; drops those outside `keep`.
   */
  fun draw(canvas: Canvas, layout: Layout, lineCount: Int, draw: IntRange, keep: IntRange) {
    val count = (lineCount + LINES - 1) / LINES
    while (nodes.size > count) nodes.removeAt(nodes.size - 1).discardDisplayList()
    while (nodes.size < count) nodes.add(RenderNode("StreamTextBlock"))
    if (recorded.size != count) recorded = recorded.copyOf(count)
    for (i in dirtyFrom until count) recorded[i] = false
    dirtyFrom = count
    for (i in 0 until count) {
      if (recorded[i] && i !in keep) {
        nodes[i].discardDisplayList()
        recorded[i] = false
      }
    }
    for (i in draw) {
      if (i !in 0 until count) continue
      val node = nodes[i]
      // HWUI frees the display list of a node no frame drew (a block off the screen for a while,
      // or a frame with the selection overlay up): record it again then.
      if (!recorded[i] || !node.hasDisplayList()) {
        val first = i * LINES
        val top = layout.getLineTop(first)
        val bottom = layout.getLineTop(min(first + LINES, lineCount))
        node.setPosition(0, top, layout.width, bottom)
        val recording = node.beginRecording()
        try {
          // Layout.draw picks the lines in the clip, which is the block's bounds.
          recording.translate(0f, -top.toFloat())
          layout.draw(recording)
        } finally {
          node.endRecording()
        }
        recorded[i] = true
      }
      canvas.drawRenderNode(node)
    }
    drawnFirst = if (draw.isEmpty()) 0 else draw.first
    drawnLast = if (draw.isEmpty()) -1 else draw.last
  }

  companion object {
    const val LINES = 32

    /** Blocks of the first `lineCount` lines of `layout` covering y in [top, bottom], or none. */
    fun range(layout: Layout, lineCount: Int, top: Int, bottom: Int): IntRange {
      if (lineCount == 0) return IntRange.EMPTY
      val height = layout.getLineTop(lineCount)
      if (bottom < 0 || top > height) return IntRange.EMPTY
      val first = min(layout.getLineForVertical(max(top, 0)), lineCount - 1) / LINES
      val last = min(layout.getLineForVertical(min(bottom, height)), lineCount - 1) / LINES
      return first..last
    }
  }
}
