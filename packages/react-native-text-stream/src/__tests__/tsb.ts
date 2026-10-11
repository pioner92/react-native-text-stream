// Builds TSB1 buffers in JS for tests (layout in ios/TextStreamBuffer.h).
import { TSB_HEADER_SIZE } from '../buffer'

export const TSB_STATUS = { open: 0, done: 1, error: 2, cancelled: 3, overflow: 4 } as const

export function tsbBuffer(
  content: Uint8Array | string,
  { status = TSB_STATUS.open, capacity }: { status?: number; capacity?: number } = {}
): ArrayBuffer {
  const bytes = typeof content === 'string' ? new TextEncoder().encode(content) : content
  const cap = capacity ?? Math.max(bytes.length, 16)
  const buffer = new ArrayBuffer(TSB_HEADER_SIZE + cap)
  const header = new DataView(buffer)
  header.setUint32(0, 0x31425354, true) // "TSB1"
  header.setUint16(4, 1, true) // version
  header.setUint16(6, TSB_HEADER_SIZE, true)
  header.setUint32(8, bytes.length % 2 ** 32, true)
  header.setUint32(12, Math.floor(bytes.length / 2 ** 32), true)
  header.setUint32(16, cap % 2 ** 32, true)
  header.setUint32(24, status, true)
  new Uint8Array(buffer, TSB_HEADER_SIZE).set(bytes)
  return buffer
}
