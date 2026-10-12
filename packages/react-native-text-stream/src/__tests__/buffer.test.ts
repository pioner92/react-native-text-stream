import { TextDecoder as NodeTextDecoder } from 'node:util'
import { bufferText, committedBytes, decodeUtf8, decodeUtf8Fallback, headerStatus, TSB_HEADER_SIZE } from '../buffer'
import { TSB_STATUS, tsbBuffer } from './tsb'

const mockBuffers = require('react-native/Libraries/TurboModule/TurboModuleRegistry').buffers

const reference = (bytes: Uint8Array) => new NodeTextDecoder('utf-8').decode(bytes)
const bytes = (...values: number[]) => new Uint8Array(values)

/** Deterministic PRNG (mulberry32), so a failing fuzz case reproduces. */
function prng(seed: number) {
  return () => {
    seed = (seed + 0x6d2b79f5) | 0
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

describe('TSB1 header', () => {
  it('reads native-owned state from an acquire snapshot, then only the committed bytes in place', () => {
    const buffer = tsbBuffer('stale', { status: TSB_STATUS.open })
    mockBuffers.readBuffer.mockReturnValue({ bytes: 4, status: TSB_STATUS.done, serial: 42 })
    try {
      expect(committedBytes(buffer)).toBe(4)
      expect(headerStatus(buffer)).toBe('done')
      expect(bufferText(buffer)).toBe('stal')
      expect(mockBuffers.readBuffer).toHaveBeenLastCalledWith(buffer)
    } finally {
      mockBuffers.readBuffer.mockReset()
    }
  })
  it('reads committed as a 64-bit little-endian count', () => {
    const buffer = tsbBuffer('hello')
    expect(committedBytes(buffer)).toBe(5)
    const header = new DataView(buffer)
    header.setUint32(8, 0x89abcdef, true)
    header.setUint32(12, 0x12, true)
    expect(committedBytes(buffer)).toBe(0x12 * 2 ** 32 + 0x89abcdef)
  })

  it.each([
    [TSB_STATUS.open, 'streaming'],
    [TSB_STATUS.done, 'done'],
    [TSB_STATUS.error, 'error'],
    [TSB_STATUS.cancelled, 'cancelled'],
    [TSB_STATUS.overflow, 'truncated'],
    [99, 'error'],
  ])('maps status %i to %s', (status, expected) => {
    expect(headerStatus(tsbBuffer('', { status }))).toBe(expected)
  })
})

describe('bufferText', () => {
  it('decodes only the committed bytes', () => {
    const buffer = tsbBuffer('hello world', { capacity: 64 })
    new DataView(buffer).setUint32(8, 5, true)
    expect(bufferText(buffer)).toBe('hello')
  })

  it('is empty for a buffer smaller than the header', () => {
    expect(bufferText(new ArrayBuffer(TSB_HEADER_SIZE - 1))).toBe('')
  })

  it('never reads past the end when committed claims more than the buffer holds', () => {
    const buffer = tsbBuffer('abc', { capacity: 3 })
    new DataView(buffer).setUint32(8, 1000, true)
    expect(bufferText(buffer)).toBe('abc')
  })
})

describe('decodeUtf8Fallback matches TextDecoder', () => {
  it.each<[string, Uint8Array]>([
    ['ASCII', new TextEncoder().encode('Hello, world!')],
    ['2-byte (Cyrillic)', new TextEncoder().encode('Привет, мир')],
    ['3-byte (CJK, ⚡)', new TextEncoder().encode('漢字 ⚡️ テキスト')],
    ['4-byte (emoji, astral)', new TextEncoder().encode('🚀 🧊 📈 𝄞')],
    ['empty', bytes()],
    ['lone continuation byte', bytes(0x41, 0x80, 0x42)],
    ['invalid lead bytes', bytes(0xc0, 0xc1, 0xf5, 0xf8, 0xfe, 0xff, 0x41)],
    ['broken 2-byte sequence', bytes(0xc3, 0x41)],
    ['broken 3-byte sequence', bytes(0xe2, 0x82, 0x41)],
    ['broken 4-byte sequence', bytes(0xf0, 0x9f, 0x9a, 0x41)],
    ['overlong encoding', bytes(0xe0, 0x80, 0x80, 0xc0, 0xaf)],
    ['UTF-16 surrogate', bytes(0xed, 0xa0, 0x80, 0xed, 0xbf, 0xbf)],
    ['above U+10FFFF', bytes(0xf4, 0x90, 0x80, 0x80)],
    ['largest code point', bytes(0xf4, 0x8f, 0xbf, 0xbf)],
    ['truncated tail', bytes(0x41, 0xf0, 0x9f, 0x9a)],
    ['leading BOM (dropped)', bytes(0xef, 0xbb, 0xbf, 0x41)],
    ['two leading BOMs (the second kept)', bytes(0xef, 0xbb, 0xbf, 0xef, 0xbb, 0xbf, 0x41)],
    ['BOM after text (kept)', bytes(0x41, 0xef, 0xbb, 0xbf, 0x42)],
    ['truncated BOM', bytes(0xef, 0xbb)],
    ['BOM, then invalid bytes', bytes(0xef, 0xbb, 0xbf, 0xff, 0x41)],
  ])('%s', (_, input) => {
    expect(decodeUtf8Fallback(input)).toBe(reference(input))
  })

  it('on random bytes', () => {
    const random = prng(42)
    for (let run = 0; run < 2000; run++) {
      const input = new Uint8Array(1 + Math.floor(random() * 32))
      // Mostly bytes near the UTF-8 boundaries, where decoders disagree.
      for (let i = 0; i < input.length; i++) {
        input[i] = random() < 0.5 ? Math.floor(random() * 256) : 0x7f + Math.floor(random() * 0x80)
      }
      expect(decodeUtf8Fallback(input)).toBe(reference(input))
    }
  })

  it('on random valid text, whole and split at every offset', () => {
    const random = prng(7)
    const alphabet = ['a', ' ', 'é', 'ж', '漢', '⚡', '🚀', '𝄞', '\n']
    for (let run = 0; run < 200; run++) {
      let text = ''
      for (let i = 0; i < 12; i++) text += alphabet[Math.floor(random() * alphabet.length)]
      const encoded = new TextEncoder().encode(text)
      expect(decodeUtf8Fallback(encoded)).toBe(text)
      // A prefix cut inside a sequence ends in one U+FFFD, exactly like TextDecoder.
      for (let cut = 0; cut <= encoded.length; cut++) {
        const prefix = encoded.subarray(0, cut)
        expect(decodeUtf8Fallback(prefix)).toBe(reference(prefix))
      }
    }
  })

  it('handles long input without blowing the stack', () => {
    const long = new TextEncoder().encode('🚀ж'.repeat(200_000))
    expect(decodeUtf8Fallback(long)).toBe('🚀ж'.repeat(200_000))
  })
})

describe('decodeUtf8', () => {
  it('uses the global TextDecoder when there is one', () => {
    const decode = jest.fn(() => 'from TextDecoder')
    const original = globalThis.TextDecoder
    ;(globalThis as any).TextDecoder = jest.fn(() => ({ decode }))
    try {
      expect(decodeUtf8(bytes(0x41))).toBe('from TextDecoder')
    } finally {
      globalThis.TextDecoder = original
    }
  })

  it('falls back without one', () => {
    const original = globalThis.TextDecoder
    delete (globalThis as any).TextDecoder
    try {
      expect(decodeUtf8(bytes(0xd0, 0xb6, 0xf5))).toBe('ж�')
    } finally {
      globalThis.TextDecoder = original
    }
  })
})
