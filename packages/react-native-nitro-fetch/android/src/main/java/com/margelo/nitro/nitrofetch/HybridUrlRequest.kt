package com.margelo.nitro.nitrofetch

import com.facebook.proguard.annotations.DoNotStrip
import org.chromium.net.UrlRequest as CronetUrlRequest
import java.nio.ByteBuffer

@DoNotStrip
class HybridUrlRequest(
  private val cronetRequest: CronetUrlRequest,
  private val byteBuffer: ByteBuffer,
  private val nativeBuffer: NativeTextStreamBuffer? = null
) : HybridUrlRequestSpec() {

  override fun start() {
    cronetRequest.start()
  }

  override fun followRedirect() {
    cronetRequest.followRedirect()
  }

  override fun read() {
    // Native mode reads on its own (HybridUrlRequestBuilder), like URLSession on iOS.
    if (nativeBuffer != null) return
    // Prepare buffer for writing (position=0, limit=capacity)
    byteBuffer.clear()
    // Pass to Cronet to fill
    cronetRequest.read(byteBuffer)
  }

  override fun cancel() {
    cronetRequest.cancel()
  }

  override fun isDone(): Boolean {
    return cronetRequest.isDone
  }

  override fun getNativeBuffer(): com.margelo.nitro.core.ArrayBuffer? = nativeBuffer?.arrayBuffer
}
