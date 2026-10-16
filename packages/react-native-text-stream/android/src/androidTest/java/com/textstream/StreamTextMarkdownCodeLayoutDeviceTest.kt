package com.textstream

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.facebook.react.uimanager.DisplayMetricsHolder
import com.facebook.react.uimanager.PixelUtil
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device-level guard for StaticLayout's LineHeightSpan metric carry across a wrapped fence. */
@RunWith(AndroidJUnit4::class)
class StreamTextMarkdownCodeLayoutDeviceTest {
  @Test fun unknownAndStreamingFenceReserveTheSameCodeHeaderBand() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    DisplayMetricsHolder.initDisplayMetricsIfNotInitialized(context)
    val style = StreamTextStyle(fontSize = 16f, lineHeight = 24f)
    fun document(language: Int) = StreamTextMarkdown("code\n", intArrayOf(0), intArrayOf(4),
        intArrayOf(StreamTextMarkdown.CODE_BLOCK or language), IntArray(1), IntArray(1), arrayOf(null))
    fun headerCount(document: StreamTextMarkdown) = (document.styled(0xff101217.toInt(), style = style)
        as android.text.Spanned).getSpans(0, 4, StreamTextMarkdown.MarkdownCodeHeaderSpan::class.java).size
    val unknown = document(0)
    val python = document(StreamTextMarkdown.LANGUAGE_PYTHON)
    assertEquals(1, headerCount(unknown))
    assertEquals(1, headerCount(python))
    fun height(document: StreamTextMarkdown) = style.staticLayout(document.styled(0xff101217.toInt(), style = style),
        style.paint(context.assets), 300).height
    assertEquals(height(unknown), height(python))
  }

  @Test fun wrappedHeadingUsesScaledExplicitLineHeight() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    DisplayMetricsHolder.initDisplayMetricsIfNotInitialized(context)
    val text = "A heading deliberately wraps across more than one visual line"
    val style = StreamTextStyle(fontSize = 16f, lineHeight = 20f)
    val document = StreamTextMarkdown(text, intArrayOf(0), intArrayOf(text.length), intArrayOf(0),
        intArrayOf(1), IntArray(1), arrayOf(null))
    val layout = style.staticLayout(document.styled(0xff101217.toInt(), style = style), style.paint(context.assets), 140)
    assertTrue(layout.lineCount > 1)
    val pitch = layout.getLineBaseline(1) - layout.getLineBaseline(0)
    val expected = kotlin.math.ceil(PixelUtil.toPixelFromDIP(20f) * 1.55f).toInt()
    assertTrue("heading pitch=$pitch expected=$expected", pitch >= expected)
  }

  @Test fun wrappedFenceHeaderKeepsNormalInteriorPitch() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    DisplayMetricsHolder.initDisplayMetricsIfNotInitialized(context)
    val text = "before\nconst answer = streamFetch(endpoint, { streamId: 'answer' });\n// Tokens keep arriving without a React render per chunk.\nafter"
    val codeStart = text.indexOf("const")
    val codeEnd = text.indexOf("after")
    fun document(flags: Int, split: Boolean) = StreamTextMarkdown(
        text, if (split) intArrayOf(codeStart, text.indexOf("streamFetch"), text.indexOf("//")) else intArrayOf(codeStart),
        if (split) intArrayOf(text.indexOf("streamFetch") - codeStart, text.indexOf("//") - text.indexOf("streamFetch"), codeEnd - text.indexOf("//")) else intArrayOf(codeEnd - codeStart),
        if (split) IntArray(3) { flags } else intArrayOf(flags),
        intArrayOf(0), intArrayOf(0), arrayOf(null))
    for (lineHeight in listOf(0f, 24f)) for (split in listOf(false, true)) {
      val style = StreamTextStyle(fontSize = 16f, lineHeight = lineHeight)
      fun layout(flags: Int) = style.staticLayout(document(flags, split).styled(0xfff2f4f8.toInt(), style = style), style.paint(context.assets), 250)
      val plain = layout(StreamTextMarkdown.CODE_BLOCK or StreamTextMarkdown.CODE)
      val layout = layout(StreamTextMarkdown.CODE_BLOCK or StreamTextMarkdown.CODE or (1 shl 9))
      val first = layout.getLineForOffset(codeStart)
      assertTrue(layout.getLineEnd(first) < text.indexOf('\n', codeStart))
      val firstDelta = layout.getLineBaseline(first + 1) - layout.getLineBaseline(first)
      val secondDelta = layout.getLineBaseline(first + 2) - layout.getLineBaseline(first + 1)
      assertEquals("continuations lineHeight=$lineHeight split=$split", secondDelta, firstDelta)
      val plainFirst = plain.getLineForOffset(codeStart)
      assertEquals("code pitch lineHeight=$lineHeight split=$split", plain.getLineBaseline(plainFirst + 1) - plain.getLineBaseline(plainFirst), firstDelta)
      // An unlabelled/streaming fence now reserves the same CODE header band as a labelled one,
      // so completing its info string changes label text but cannot move the code surface.
      val headerPixels = layout.getLineBaseline(first) - plain.getLineBaseline(plainFirst)
      assertTrue("stable header lineHeight=$lineHeight split=$split", kotlin.math.abs(headerPixels) <= 1)
    }
  }
}
