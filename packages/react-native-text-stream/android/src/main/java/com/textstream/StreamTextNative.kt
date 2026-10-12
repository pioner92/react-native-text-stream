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

  /** The `source` prop as of this state (a StateWrapper), or null. */
  @JvmStatic external fun stateSource(stateWrapper: Any): StreamTextSource?

  @JvmStatic external fun stateGeneration(stateWrapper: Any): Int

  /** Commits the view's text height (dp) on the calling (main) thread, without the JS thread. */
  @JvmStatic external fun updateHeight(stateWrapper: Any, height: Float, revision: Int)

  @JvmStatic external fun isValid(base: Long, size: Long): Boolean

  @JvmStatic external fun capacity(base: Long): Long

  /** Acquire load: the bytes below it are published. */
  @JvmStatic external fun committed(base: Long): Long

  /** Acquire load: once terminal, a `committed` read after it is final. */
  @JvmStatic external fun status(base: Long): Int

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

  fun release() {
    mHybridData.resetNative()
  }
}
