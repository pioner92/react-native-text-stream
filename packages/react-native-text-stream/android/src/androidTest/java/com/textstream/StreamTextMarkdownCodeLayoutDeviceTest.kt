package com.textstream

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.facebook.react.uimanager.DisplayMetricsHolder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device-level guard for StaticLayout's LineHeightSpan metric carry across a wrapped fence. */
@RunWith(AndroidJUnit4::class)
class StreamTextMarkdownCodeLayoutDeviceTest {
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
      val headerPixels = layout.getLineBaseline(first) - plain.getLineBaseline(plainFirst)
      val expectedHeader = (20 * context.resources.displayMetrics.density + .5f).toInt()
      assertTrue("header lineHeight=$lineHeight split=$split", kotlin.math.abs(headerPixels - expectedHeader) <= 1)
    }
  }
}
