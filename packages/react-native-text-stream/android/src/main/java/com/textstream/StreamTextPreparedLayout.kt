package com.textstream

import android.content.ComponentCallbacks2
import android.content.Context
import android.content.res.AssetManager
import android.content.res.Configuration
import android.text.StaticLayout
import android.text.TextPaint
import com.facebook.proguard.annotations.DoNotStrip
import com.facebook.react.uimanager.PixelUtil
import java.nio.ByteBuffer
import kotlin.math.ceil
import kotlin.math.max

/**
 * A finished text laid out ahead, in the shadow node's measure (cpp: StreamTextPreparedAndroid),
 * off the main thread: the view draws it as is, without laying the text out again.
 */
@DoNotStrip
class StreamTextPreparedLayout private constructor(
    val text: CharSequence,
    val layout: StaticLayout,
    /** The layout's own paint: the view sets its color before drawing. */
    val paint: TextPaint,
    val style: StreamTextStyle,
    /** Pixels: the layout's wrap width. */
    val widthPx: Int,
    /** Committed bytes it was made from. */
    val bytes: Long,
    val markdown: Boolean,
    val markdownFlavor: String,
    val rawText: String,
    internal val markdownDocument: StreamTextMarkdown?,
    val color: Int,
) {
  /** Dp, what measure reports. Like <Text>, no text has no height. */
  @get:DoNotStrip
  val height: Float = if (text.isEmpty()) 0f else PixelUtil.toDIPFromPixel(layout.height.toFloat())

  /** Its lines fit a view `width` px wide, of this `style`, as they would be wrapped there. */
  fun fits(width: Int, style: StreamTextStyle, @Suppress("UNUSED_PARAMETER") color: Int): Boolean {
    if (style != this.style) return false
    return when (width) {
      widthPx -> true
      // Fabric rounds the frame and the padding to pixels apart from each other: a pixel off keeps
      // these lines, as long as none of them overflows a narrower view.
      widthPx + 1 -> true
      widthPx - 1 -> maxLineWidth <= width
      else -> false
    }
  }

  private val maxLineWidth: Float by lazy {
    var widest = 0f
    for (i in 0 until layout.lineCount) widest = max(widest, layout.getLineWidth(i))
    widest
  }

  companion object {
    @Volatile private var assets: AssetManager? = null
    internal val fontAssets: AssetManager? get() = assets

    /** Fonts come from the app's assets; called with the app's context. */
    internal fun init(context: Context) {
      if (assets != null) return
      assets = context.applicationContext.assets
      // Under memory pressure the cache goes first, as on iOS.
      context.applicationContext.registerComponentCallbacks(object : ComponentCallbacks2 {
        override fun onTrimMemory(level: Int) {
          // Not for UI_HIDDEN alone: going to the background is no memory pressure.
          @Suppress("DEPRECATION")
          if (level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW && level != ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN) {
            StreamTextNative.clearPreparedCache(false)
            StreamTextView.trimAllMemory()
          }
        }

        override fun onConfigurationChanged(newConfig: Configuration) {}

        @Deprecated("Deprecated in Java")
        override fun onLowMemory() {
          StreamTextNative.clearPreparedCache(false)
          StreamTextView.trimAllMemory()
        }
      })
    }

    /**
     * Called from C++ on a prepare thread, the JS thread or the main thread: the committed UTF-8 at
     * `data[64, 64 + bytes)`, laid out `widthDp` wide in whole pixels rounded up, as Yoga rounds
     * a measured node's frame: the width the view will have.
     */
    @JvmStatic
    @DoNotStrip
    fun create(
        data: ByteBuffer,
        bytes: Int,
        widthDp: Float,
        fontSize: Float,
        fontFamily: String?,
        fontWeight: String?,
        lineHeight: Float,
        letterSpacing: Float,
        color: Int,
        textAlign: String?,
        format: String?,
        markdownFlavor: String?,
    ): StreamTextPreparedLayout? {
      val started = if (StreamTextDebug.enabled) System.nanoTime() else 0L
      val utf8 = ByteArray(bytes)
      data.duplicate().apply { position(StreamTextNative.HEADER_SIZE) }.get(utf8, 0, bytes)
      val out = StringBuilder(bytes)
      val decoder = Utf8StreamDecoder()
      decoder.decode(utf8, 0, bytes, out)
      decoder.finish(out)
      val rawText = out.toString()
      val markdown = format == "markdown"
      // This runs on the existing prepare worker; spans and StaticLayout are ready before mount.
      val document = if (markdown) StreamTextNative.parseMarkdownStreaming(rawText, markdownFlavor == "gfm", true) else null
      // Rich attachments require view-owned loading and width-aware layout; a prepared canvas
      // cannot safely cache mutable image spans or a table measured before its view has a width.
      if (document?.hasRichBlocks == true) return null
      val text: CharSequence = document?.styled(color) ?: rawText
      val decoded = if (started != 0L) System.nanoTime() else 0L
      val style = StreamTextStyle(
          fontSize, fontFamily?.ifEmpty { null }, fontWeight?.ifEmpty { null }, lineHeight, letterSpacing,
          textAlign?.ifEmpty { null })
      val widthPx = max(1, ceil(PixelUtil.toPixelFromDIP(widthDp) - 0.001f).toInt())
      val paint = style.paint(assets)
      if (started != 0L) {
        StreamTextDebug.addPhase("prepare.decode", decoded - started)
        StreamTextDebug.addPhase("prepare.paint", System.nanoTime() - decoded)
      }
      val prepared = StreamTextPreparedLayout(text, style.staticLayout(text, paint, widthPx), paint, style, widthPx, bytes.toLong(), markdown, markdownFlavor ?: "commonmark", rawText, document, color)
      if (started != 0L) {
        val time = System.nanoTime() - started
        StreamTextDebug.addPhase("prepare.staticLayout", time)
        // Where it ran: in measure on the JS thread, the main thread, or ahead on a prepare thread.
        val thread = Thread.currentThread()
        val where = when {
          thread === android.os.Looper.getMainLooper().thread -> "main".also { StreamTextDebug.noteMainLayout(time) }
          thread.name == "StreamTextPrep" -> "prepareThread"
          else -> "js"
        }
        StreamTextDebug.addPhase("prepare.on.$where", time)
      }
      return prepared
    }
  }
}
