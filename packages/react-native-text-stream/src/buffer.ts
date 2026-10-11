// Reading a TSB1 buffer from JS (layout in ios/TextStreamBuffer.h).
import type { StreamTextStatus } from './types'

export const TSB_HEADER_SIZE = 64

/** TSB1 header: `committed` is a little-endian u64 at offset 8. */
export function committedBytes(buffer: ArrayBuffer): number {
  const header = new DataView(buffer, 0, TSB_HEADER_SIZE)
  return header.getUint32(8, true) + header.getUint32(12, true) * 2 ** 32
}

/** TSB1 header: `status` is a little-endian u32 at offset 24. */
export function headerStatus(buffer: ArrayBuffer): StreamTextStatus {
  switch (new DataView(buffer, 0, TSB_HEADER_SIZE).getUint32(24, true)) {
    case 0:
      return 'streaming'
    case 1:
      return 'done'
    case 3:
      return 'cancelled'
    case 4:
      return 'truncated'
    default:
      return 'error'
  }
}

/** The text received so far: the committed bytes, decoded as UTF-8. */
export function bufferText(buffer: ArrayBuffer): string {
  if (buffer.byteLength < TSB_HEADER_SIZE) return ''
  const bytes = Math.min(committedBytes(buffer), buffer.byteLength - TSB_HEADER_SIZE)
  return decodeUtf8(new Uint8Array(buffer, TSB_HEADER_SIZE, bytes))
}

type Decoder = new (label: string) => { decode(bytes: Uint8Array): string }

export function decodeUtf8(bytes: Uint8Array): string {
  const NativeDecoder = (globalThis as { TextDecoder?: Decoder }).TextDecoder
  return NativeDecoder ? new NativeDecoder('utf-8').decode(bytes) : decodeUtf8Fallback(bytes)
}

/**
 * The WHATWG UTF-8 decoder (what TextDecoder does), for runtimes without TextDecoder: each
 * maximal invalid subsequence becomes one U+FFFD, as the native view shows it.
 */
export function decodeUtf8Fallback(bytes: Uint8Array): string {
  const units: number[] = []
  let out = ''
  const flush = () => {
    out += String.fromCharCode(...units)
    units.length = 0
  }
  let codePoint = 0
  let needed = 0
  let seen = 0
  let lower = 0x80
  let upper = 0xbf
  for (let i = 0; i < bytes.length; i++) {
    const byte = bytes[i]!
    if (needed === 0) {
      if (byte <= 0x7f) {
        units.push(byte)
      } else if (byte >= 0xc2 && byte <= 0xdf) {
        needed = 1
        codePoint = byte & 0x1f
      } else if (byte >= 0xe0 && byte <= 0xef) {
        if (byte === 0xe0) lower = 0xa0
        if (byte === 0xed) upper = 0x9f
        needed = 2
        codePoint = byte & 0xf
      } else if (byte >= 0xf0 && byte <= 0xf4) {
        if (byte === 0xf0) lower = 0x90
        if (byte === 0xf4) upper = 0x8f
        needed = 3
        codePoint = byte & 0x7
      } else {
        units.push(0xfffd)
      }
    } else if (byte < lower || byte > upper) {
      // The bytes so far are one invalid subsequence; this byte starts over.
      codePoint = needed = seen = 0
      lower = 0x80
      upper = 0xbf
      units.push(0xfffd)
      i--
      continue
    } else {
      lower = 0x80
      upper = 0xbf
      codePoint = (codePoint << 6) | (byte & 0x3f)
      if (++seen === needed) {
        if (codePoint > 0xffff) {
          codePoint -= 0x10000
          units.push(0xd800 + (codePoint >> 10), 0xdc00 + (codePoint & 0x3ff))
        } else {
          units.push(codePoint)
        }
        codePoint = needed = seen = 0
      }
    }
    if (units.length >= 4096) flush()
  }
  if (needed > 0) units.push(0xfffd)
  flush()
  return out
}
