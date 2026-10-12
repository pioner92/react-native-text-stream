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
    get() = recorded.count { it }

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

  /** Blocks of `layout` covering layout y in [top, bottom], or an empty range. */
  fun range(layout: Layout, top: Int, bottom: Int): IntRange {
    val height = layout.height
    if (bottom < 0 || top > height || layout.lineCount == 0) return IntRange.EMPTY
    val first = layout.getLineForVertical(max(top, 0)) / LINES
    val last = layout.getLineForVertical(min(bottom, height)) / LINES
    return first..last
  }

  /** Draws the blocks in `draw`, recording those not current; drops those outside `keep`. */
  fun draw(canvas: Canvas, layout: Layout, draw: IntRange, keep: IntRange) {
    val lineCount = layout.lineCount
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
      if (!recorded[i]) {
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
  }
}
