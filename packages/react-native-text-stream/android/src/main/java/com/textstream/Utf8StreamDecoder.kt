package com.textstream

/**
 * The WHATWG UTF-8 decoder (what TextDecoder does), fed chunk by chunk: its state carries a
 * sequence split across chunks, so any split gives the text of decoding the whole at once. Each
 * maximal invalid subsequence becomes one U+FFFD, and only a BOM opening the stream is dropped.
 */
internal class Utf8StreamDecoder {
  private var codePoint = 0
  private var needed = 0
  private var seen = 0
  private var lower = 0x80
  private var upper = 0xBF
  private var atStart = true

  fun reset() {
    codePoint = 0
    needed = 0
    seen = 0
    lower = 0x80
    upper = 0xBF
    atStart = true
  }

  /** Decodes `bytes[from, to)` into `out`. */
  fun decode(bytes: ByteArray, from: Int, to: Int, out: StringBuilder) {
    var i = from
    while (i < to) {
      val byte = bytes[i].toInt() and 0xFF
      if (needed == 0) {
        when {
          byte <= 0x7F -> emit(byte, out)
          byte in 0xC2..0xDF -> {
            needed = 1
            codePoint = byte and 0x1F
          }
          byte in 0xE0..0xEF -> {
            if (byte == 0xE0) lower = 0xA0
            if (byte == 0xED) upper = 0x9F
            needed = 2
            codePoint = byte and 0xF
          }
          byte in 0xF0..0xF4 -> {
            if (byte == 0xF0) lower = 0x90
            if (byte == 0xF4) upper = 0x8F
            needed = 3
            codePoint = byte and 0x7
          }
          else -> emit(0xFFFD, out)
        }
      } else if (byte < lower || byte > upper) {
        // The bytes so far are one invalid subsequence; this byte starts over.
        resetSequence()
        emit(0xFFFD, out)
        continue
      } else {
        lower = 0x80
        upper = 0xBF
        codePoint = (codePoint shl 6) or (byte and 0x3F)
        if (++seen == needed) {
          val complete = codePoint
          resetSequence()
          emit(complete, out)
        }
      }
      i++
    }
  }

  /** The stream ended: a sequence it cut short is one U+FFFD. */
  fun finish(out: StringBuilder) {
    if (needed > 0) {
      resetSequence()
      emit(0xFFFD, out)
    }
  }

  private fun resetSequence() {
    codePoint = 0
    needed = 0
    seen = 0
    lower = 0x80
    upper = 0xBF
  }

  private fun emit(codePoint: Int, out: StringBuilder) {
    if (atStart) {
      atStart = false
      if (codePoint == 0xFEFF) return
    }
    out.appendCodePoint(codePoint)
  }
}
