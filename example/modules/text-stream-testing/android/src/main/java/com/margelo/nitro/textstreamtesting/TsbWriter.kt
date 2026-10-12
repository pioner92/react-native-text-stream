package com.margelo.nitro.textstreamtesting

import com.facebook.proguard.annotations.DoNotStrip
import java.nio.ByteBuffer

/** TSB1 writes with the release stores of TextStreamBuffer.h (src/main/cpp/cpp-adapter.cpp). */
@DoNotStrip
internal object TsbWriter {
  @JvmStatic external fun address(buffer: ByteBuffer): Long

  @JvmStatic external fun init(base: Long, capacity: Long)

  @JvmStatic external fun append(base: Long, bytes: ByteArray, offset: Int, length: Int): Boolean

  @JvmStatic external fun setStatus(base: Long, status: Int)
}
