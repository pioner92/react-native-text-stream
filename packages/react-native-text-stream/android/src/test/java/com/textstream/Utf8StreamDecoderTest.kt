package com.textstream

import org.junit.Assert.assertEquals
import org.junit.Test

/** The vectors come from TextDecoder (scripts/utf8-vectors.mjs), as the JS and iOS decoders are tested. */
class Utf8StreamDecoderTest {
  private val cases: List<Pair<ByteArray, String>> =
      javaClass.classLoader!!.getResource("utf8-vectors.tsv")!!.readText().lines().filter { it.isNotEmpty() }.map {
        val (input, expected) = it.split('\t').let { parts -> parts[0] to parts.getOrElse(1) { "" } }
        input.chunked(2).map { byte -> byte.toInt(16).toByte() }.toByteArray() to
            expected.chunked(4).map { unit -> unit.toInt(16).toChar() }.joinToString("")
      }

  private fun decode(bytes: ByteArray, cuts: List<Int>): String {
    val decoder = Utf8StreamDecoder()
    val out = StringBuilder()
    var from = 0
    for (cut in cuts + bytes.size) {
      decoder.decode(bytes, from, cut, out)
      from = cut
    }
    decoder.finish(out)
    return out.toString()
  }

  @Test
  fun decodesLikeTextDecoder() {
    for ((input, expected) in cases) {
      assertEquals(input.joinToString(" ") { "%02x".format(it) }, expected, decode(input, emptyList()))
    }
  }

  @Test
  fun anySplitGivesTheSameText() {
    for ((input, expected) in cases) {
      for (cut in 0..input.size) {
        assertEquals(expected, decode(input, listOf(cut)))
      }
      // Byte by byte: every sequence and BOM is split.
      assertEquals(expected, decode(input, (1 until input.size).toList()))
    }
  }

  @Test
  fun resetStartsANewStream() {
    val decoder = Utf8StreamDecoder()
    val out = StringBuilder()
    decoder.decode(byteArrayOf(0x41, 0xF0.toByte()), 0, 2, out)
    decoder.reset()
    // The cut-off sequence is dropped, and a BOM opens the new stream again.
    decoder.decode(byteArrayOf(0xEF.toByte(), 0xBB.toByte(), 0xBF.toByte(), 0x42), 0, 4, out)
    decoder.finish(out)
    assertEquals("AB", out.toString())
  }
}
