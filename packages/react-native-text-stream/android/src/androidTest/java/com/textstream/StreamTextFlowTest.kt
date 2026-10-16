package com.textstream

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.text.SpannableString
import android.text.Spanned
import android.text.style.StyleSpan
import android.graphics.Typeface
import com.facebook.react.uimanager.DisplayMetricsHolder
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Runs on Android: JVM android.jar stubs cannot exercise DynamicLayout. */
@RunWith(AndroidJUnit4::class)
class StreamTextFlowTest {
  @Test
  fun replaceFromRemovesStaleSuffixSpansAndKeepsPrefixLayout() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    var failure: Throwable? = null
    instrumentation.runOnMainSync {
      try {
        DisplayMetricsHolder.initDisplayMetricsIfNotInitialized(instrumentation.targetContext)
        val style = StreamTextStyle(fontSize = 16f, lineHeight = 22f)
        val flow = StreamTextFlow()
        flow.rebuild(style, style.paint(instrumentation.targetContext.assets), 400)
        val prefix = SpannableString("kept prefix ").apply {
          setSpan(StyleSpan(Typeface.BOLD), 0, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        org.junit.Assert.assertTrue(flow.append(prefix, style.withLineHeight(prefix)))
        val suffixStart = prefix.length
        repeat(100) { index ->
          val source = SpannableString("token-$index").apply {
            setSpan(StyleSpan(Typeface.BOLD), 0, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
          }
          org.junit.Assert.assertTrue(flow.replaceFrom(suffixStart, style.withLineHeight(source)))
          val spans = flow.text.getSpans(0, flow.text.length, StyleSpan::class.java)
          assertEquals("replacement $index", 2, spans.size)
          assertEquals(0, flow.text.getSpanStart(spans[0]))
          assertEquals(suffixStart, flow.text.getSpanEnd(spans[0]))
          assertEquals(suffixStart, flow.text.getSpanStart(spans[1]))
          assertEquals(flow.text.length, flow.text.getSpanEnd(spans[1]))
          val expected = style.staticLayout(flow.text, style.paint(instrumentation.targetContext.assets), 400)
          flow.forEachSection { actual, lines, _, _ ->
            val laidOut = actual.text as Spanned
            assertEquals("live spans $index", 2, laidOut.getSpans(0, laidOut.length, StyleSpan::class.java).size)
            assertEquals("live lines $index", expected.lineCount, lines)
            for (line in 0 until lines) {
              assertEquals("live line $line at replacement $index", expected.getLineStart(line), actual.getLineStart(line))
            }
          }
        }
      } catch (e: Throwable) { failure = e }
    }
    failure?.let { throw it }
  }

  @Test
  fun testChunkedParagraphsMatchStaticLayout() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    var failure: Throwable? = null
    instrumentation.runOnMainSync {
      try {
        DisplayMetricsHolder.initDisplayMetricsIfNotInitialized(instrumentation.targetContext)
        val text = "Let's take the slower route. Here is a day with plenty of room to wander.\n\n" +
            "09:00 — Coffee & quiet lanes\nStart with a warm coffee and a walk through Higashiyama. " +
            "Follow the little streets, pause by the wooden shopfronts, and let the morning unfold.\n\n" +
            "11:30 — A small discovery\nFind a pottery shop or a tiny garden along the way."
        for (size in listOf(14.5f, 18f)) {
          for (chunkSize in listOf(1, 4, 17)) {
            val style = StreamTextStyle(fontSize = size, lineHeight = size * 1.5f)
            val paint = style.paint(instrumentation.targetContext.assets)
            val flow = StreamTextFlow()
            flow.rebuild(style, paint, 800)
            var offset = 0
            while (offset < text.length) {
              val end = minOf(offset + chunkSize, text.length)
              val chunk = text.substring(offset, end)
              flow.append(chunk, style.withLineHeight(chunk))
              val expected = style.staticLayout(text.substring(0, end), style.paint(instrumentation.targetContext.assets), 800)
              flow.forEachSection { actual, lines, _, _ ->
                assertEquals("lines at $end ($size/$chunkSize)", expected.lineCount, lines)
                for (line in 0 until lines) {
                  assertEquals("line $line at $end ($size/$chunkSize)", expected.getLineStart(line), actual.getLineStart(line))
                }
              }
              offset = end
            }
          }
        }
      } catch (e: Throwable) { failure = e }
    }
    failure?.let { throw it }
  }
}
