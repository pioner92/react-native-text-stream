package com.textstream

import android.os.Build
import android.text.DynamicLayout
import android.text.Layout
import android.text.SpanWatcher
import android.text.SpannableStringBuilder
import android.text.TextPaint
import android.text.TextWatcher
import android.text.TextUtils

/**
 * The text a view lays out itself (a stream): a DynamicLayout over its end, and the lines before
 * that frozen. A DynamicLayout re-wraps the whole paragraph an append touches, so a long paragraph
 * (a log, a code dump, an answer without line breaks) cost O(paragraph) per append. Wrapping is
 * greedy (BREAK_STRATEGY_SIMPLE): an append never moves a line break above the line it continues.
 * So once the paragraph the stream is in passes [FREEZE_LINES] lines, the DynamicLayout stops
 * there: it keeps its text and shows all but its last [KEEP_LINES] lines, as they stand (nothing
 * is laid out again), and a new one takes the tail.
 *
 * Not with right-to-left text (the tail's paragraph direction would have to carry over) or
 * justify (a frozen part's last line would lose it): one DynamicLayout then, as before.
 */
internal class StreamTextFlow {
  /** The whole text, without spans. */
  val text = SpannableStringBuilder()

  /** The first `lines` lines of `layout`, `top` px from the text's top. */
  class Section(
      val layout: Layout,
      val lines: Int,
      val top: Int,
      val height: Int,
      val trimTop: Int,
      val blocks: StreamTextBlocks?,
  )

  private val frozen = ArrayList<Section>()
  private var frozenHeight = 0
  /** The text after the frozen sections, with its line height spans. */
  private var live = SpannableStringBuilder()
  private var liveLayout: DynamicLayout? = null
  private var liveBlocks = newBlocks()
  private var sawRtl = false

  val isLaidOut: Boolean
    get() = liveLayout != null

  /** Px; like <Text>, no text has no height. */
  val height: Int
    get() {
      val layout = liveLayout ?: return 0
      if (text.isEmpty()) return 0
      return frozenHeight + layout.height - liveTrimTop(layout)
    }

  /** Only the text's first line may have font padding above it. */
  private fun liveTrimTop(layout: Layout) = if (frozen.isEmpty()) 0 else -layout.topPadding

  /** No text; the live layout stays (empty), so the view reports its zero height. */
  fun clear() {
    text.clear()
    live.clear()
    sawRtl = false
    dropFrozen()
    liveBlocks?.discard()
  }

  /** Lays the whole text out again, `width` px wide (a new width or style); none without a width. */
  fun rebuild(style: StreamTextStyle, paint: TextPaint, width: Int) {
    dropFrozen()
    live = SpannableStringBuilder(text)
    liveLayout = if (width > 0) style.dynamicLayout(live, paint, width) else null
    liveBlocks?.discard()
  }

  private fun dropFrozen() {
    for (section in frozen) section.blocks?.discard()
    frozen.clear()
    frozenHeight = 0
  }

  /**
   * Appends `chars` (`spanned`: with their line height spans); returns false when the text must be
   * laid out again ([rebuild]): right-to-left text arrived after lines were frozen.
   */
  fun append(chars: CharSequence, spanned: CharSequence): Boolean {
    val from = live.length
    text.append(chars)
    live.append(spanned)
    if (!sawRtl && containsRtl(chars)) {
      sawRtl = true
      if (frozen.isNotEmpty()) return false
    }
    val layout = liveLayout ?: return true
    // The line the append continued may re-wrap (one more for safety).
    liveBlocks?.markDirty(maxOf(layout.getLineForOffset(from) - 1, 0) / StreamTextBlocks.LINES)
    return true
  }

  /**
   * Replaces a Markdown suffix while retaining DynamicLayout's untouched prefix. Markdown never
   * freezes, because a delimiter may change styles above the current end. Returns false when a
   * caller must make a fresh layout (for example after a width/style rebuild left no live one).
   */
  fun replaceFrom(from: Int, replacement: CharSequence): Boolean {
    if (frozen.isNotEmpty() || from !in 0..text.length || from > live.length) return false
    val layout = liveLayout ?: return false
    // Spannable keeps some exclusive-exclusive spans at a replacement boundary. Replacing a
    // Markdown suffix then adds fresh spans beside those stale objects on every update. Remove
    // only suffix-owned formatting first; DynamicLayout's watcher spans must remain attached.
    removeSuffixSpans(text, from)
    removeSuffixSpans(live, from)
    text.replace(from, text.length, replacement)
    live.replace(from, live.length, replacement)
    liveBlocks?.markDirty(maxOf(layout.getLineForOffset(from.coerceAtMost(layout.text.length)) - 1, 0) / StreamTextBlocks.LINES)
    return true
  }

  private fun removeSuffixSpans(buffer: SpannableStringBuilder, from: Int) {
    for (span in buffer.getSpans(from, buffer.length, Any::class.java)) {
      if (buffer.getSpanStart(span) >= from && span !is TextWatcher && span !is SpanWatcher) {
        buffer.removeSpan(span)
      }
    }
  }

  /** Freezes all but the last lines once the paragraph the stream is in is long (see the class). */
  fun freezeIfLong(style: StreamTextStyle, paint: TextPaint) {
    val layout = liveLayout ?: return
    val lineCount = layout.lineCount
    if (sawRtl || style.textAlign == "justify" || lineCount <= FREEZE_LINES) return
    val paragraphStart = TextUtils.lastIndexOf(live, '\n', live.length - 1) + 1
    if (lineCount - layout.getLineForOffset(paragraphStart) <= FREEZE_LINES) return
    val keep = lineCount - KEEP_LINES
    val cut = layout.getLineStart(keep)
    // Font padding (no lineHeight) only above the text's first line; none below a frozen part.
    val trimTop = liveTrimTop(layout)
    val height = layout.getLineTop(keep) - trimTop
    // Its recorded blocks stay valid, but for the last one, which now ends at `keep`.
    liveBlocks?.markDirty((keep - 1) / StreamTextBlocks.LINES)
    frozen.add(Section(layout, keep, frozenHeight, height, trimTop, liveBlocks))
    frozenHeight += height
    // The tail goes on in a layout of its own; this one keeps its text as it stands.
    live = SpannableStringBuilder(live, cut, live.length)
    liveLayout = style.dynamicLayout(live, paint, layout.width)
    liveBlocks = newBlocks()
  }

  /** Every line changed (a new color): record every block again. */
  fun markAllDirty() {
    for (section in frozen) section.blocks?.markDirty(0)
    liveBlocks?.markDirty(0)
  }

  fun discardBlocks() {
    for (section in frozen) section.blocks?.discard()
    liveBlocks?.discard()
  }

  /** The sections top to bottom, the live one last: (layout, lines shown, y of its top, blocks). */
  fun forEachSection(action: (layout: Layout, lines: Int, y: Int, blocks: StreamTextBlocks?) -> Unit) {
    for (section in frozen) action(section.layout, section.lines, section.top - section.trimTop, section.blocks)
    val layout = liveLayout ?: return
    action(layout, layout.lineCount, frozenHeight - liveTrimTop(layout), liveBlocks)
  }

  /** Display lists held. */
  val blockCount: Int
    get() = frozen.sumOf { it.blocks?.count ?: 0 } + (liveBlocks?.count ?: 0)

  private fun newBlocks() = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) StreamTextBlocks() else null

  private fun containsRtl(chars: CharSequence): Boolean {
    for (i in 0 until chars.length) {
      val c = chars[i]
      if (c < '֐') continue
      // Hebrew to NKo and the rest of the Arabic blocks, presentation forms, and RLM/RLE/RLO/RLI.
      if (c in '֐'..'ࣿ' || c in 'יִ'..'﷿' || c in 'ﹰ'..'﻿' ||
          c == '‏' || c == '‫' || c == '‮' || c == '⁧') return true
      // Right-to-left scripts outside the BMP (U+10800–U+10FFF, U+1E800–U+1EFFF).
      if (c == '\uD802' || c == '\uD803' || c == '\uD83A' || c == '\uD83B') return true
    }
    return false
  }

  companion object {
    const val FREEZE_LINES = 96
    const val KEEP_LINES = 2
  }
}
