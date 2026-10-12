package com.margelo.nitro.nitrofetch

import androidx.annotation.Keep
import com.facebook.proguard.annotations.DoNotStrip
import com.margelo.nitro.core.ArrayBuffer
import java.nio.ByteBuffer

/** A TSB1 buffer (ios/NitroTextStreamBuffer.h) the response body is written into natively. */
class NativeTextStreamBuffer private constructor(val arrayBuffer: ArrayBuffer, private val base: Long) {

  /** Single writer: Cronet runs one callback of a request at a time. Bytes that don't fit set OVERFLOW. */
  fun append(src: ByteBuffer, length: Int) {
    if (length > 0) NativeTextStreamBufferJni.nativeAppend(base, src, length)
  }

  /** The first terminal status wins. */
  fun setStatus(status: Int) {
    NativeTextStreamBufferJni.nativeSetStatus(base, status)
  }

  companion object {
    const val STATUS_DONE = 1
    const val STATUS_ERROR = 2
    const val STATUS_CANCELLED = 3

    fun create(capacity: Int): NativeTextStreamBuffer {
      val buffer = NativeTextStreamBufferJni.nativeCreate(capacity.toLong())
      return NativeTextStreamBuffer(buffer, NativeTextStreamBufferJni.nativeAddress(buffer))
    }
  }
}

/** Implemented in cpp/NativeTextStreamBuffer.cpp. */
@DoNotStrip
@Keep
internal object NativeTextStreamBufferJni {
  @JvmStatic external fun nativeCreate(capacity: Long): ArrayBuffer
  @JvmStatic external fun nativeAddress(buffer: ArrayBuffer): Long
  @JvmStatic external fun nativeAppend(base: Long, src: ByteBuffer, length: Int): Boolean
  @JvmStatic external fun nativeSetStatus(base: Long, status: Int)
}
