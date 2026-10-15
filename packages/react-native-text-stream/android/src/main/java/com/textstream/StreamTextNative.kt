package com.textstream

import com.facebook.jni.HybridData
import com.facebook.proguard.annotations.DoNotStrip
import java.nio.ByteBuffer

/** JNI into the C++ the app builds from src/main/jni (TSB1 reads, state updates). */
@DoNotStrip
internal object StreamTextNative {
  init {
    System.loadLibrary("react_codegen_textstream")
  }

  /** The buffer an `attach` command refers to (StreamTextBuffers), taken once. */
  @JvmStatic external fun takeBuffer(id: Int): StreamTextSource?

  /** What streams under `streamId` now (streamFetch), or null. */
  @JvmStatic external fun streamSource(streamId: String): StreamTextSource?

  /** `wake` runs, on any thread, whenever a request starts streaming under `streamId`. */
  @JvmStatic external fun listenStream(streamId: String, wake: Runnable): Long

  @JvmStatic external fun unlistenStream(streamId: String, token: Long)

  /** The `source` prop as of this state (a StateWrapper), or null. */
  @JvmStatic external fun stateSource(stateWrapper: Any): StreamTextSource?

  @JvmStatic external fun stateGeneration(stateWrapper: Any): Int

  /** 0 = none, 1 = explicit source prop, 2 = named stream. */
  @JvmStatic external fun stateSourceOwner(stateWrapper: Any): Int

  /** Commits the view's text height (dp) on the calling (main) thread, without the JS thread. */
  @JvmStatic external fun updateHeight(stateWrapper: Any, height: Float, revision: Int)

  /** The layout the shadow node prepared for this state's `source`, or null. */
  @JvmStatic external fun statePrepared(stateWrapper: Any): StreamTextPreparedLayout?

  /** Lays a finished `source` out in the background for its next mount, `widthDp` wide. */
  @JvmStatic external fun prepareInBackground(
      source: StreamTextSource,
      widthDp: Float,
      fontSize: Float,
      fontFamily: String?,
      fontWeight: String?,
      lineHeight: Float,
      letterSpacing: Float,
      textAlign: String?,
      format: String?,
      markdownFlavor: String?,
      color: Int,
  )

  /** Tests and benchmarks: off, finished sources are laid out by the view, as streams are. */
  @JvmStatic external fun setPreparedEnabled(enabled: Boolean)

  /** Forgets every prepared layout; `invalidate` also those in use (a cold start). */
  @JvmStatic external fun clearPreparedCache(invalidate: Boolean)

  @JvmStatic external fun preparedCacheCount(): Int

  @JvmStatic external fun preparedCacheDump(): String

  @JvmStatic external fun isValid(base: Long, size: Long): Boolean

  @JvmStatic external fun capacity(base: Long): Long

  /** Acquire load: the bytes below it are published. */
  @JvmStatic external fun committed(base: Long): Long

  /** Acquire load: once terminal, a `committed` read after it is final. */
  @JvmStatic external fun status(base: Long): Int

  /** MD4C-backed parse. Called only by [StreamTextMarkdownWorker], never by the UI thread. */
  @JvmStatic external fun parseMarkdownStreaming(markdown: String, github: Boolean, final: Boolean): StreamTextMarkdown?

  const val STATUS_OPEN = 0
  const val STATUS_DONE = 1
  const val STATUS_CANCELLED = 3
  const val STATUS_OVERFLOW = 4
  const val HEADER_SIZE = 64
}

/** A TSB1 buffer, kept alive (natively) until [release]. */
@DoNotStrip
class StreamTextSource @DoNotStrip private constructor(@field:DoNotStrip private val mHybridData: HybridData) {
  external fun address(): Long

  external fun size(): Long

  /** The whole buffer, header included. */
  external fun bytes(): ByteBuffer

  /** Its writer (streamFetch) wakes the view on every write: nothing needs to poll it. */
  external fun isPushed(): Boolean

  /** A streamFetch() buffer's unique number (an address may be reused once one is freed), or 0. */
  external fun serial(): Long

  /** `wake` runs on the writer's thread after each write: post, don't work. 0 if not pushed. */
  external fun observe(wake: Runnable): Long

  external fun unobserve(token: Long)

  fun release() {
    mHybridData.resetNative()
  }
}
