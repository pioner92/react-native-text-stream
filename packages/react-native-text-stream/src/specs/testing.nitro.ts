import type { HybridObject } from 'react-native-nitro-modules'

/** What a StreamTextView shows, read on the main thread. */
export interface StreamTextSnapshot {
  /** The text in the view. */
  text: string
  /** The height last reported to layout. */
  height: number
  /** The last status reported to JS; empty before the first one. */
  status: string
  /** False until the view got its first text (the text view is created lazily). */
  hasTextView: boolean
  /** The font size, text color (#RRGGBBAA) and selectability the text view uses now. */
  fontSize: number
  color: string
  selectable: boolean
  /** What VoiceOver reads. */
  accessibilityValue: string
}

/**
 * Tests only: a producer that writes TSB1 buffers the way nitro-fetch does (release stores from
 * a background queue), and a way to read what a StreamTextView with `recordDisplayLog` shows.
 */
export interface StreamTextTesting extends HybridObject<{ ios: 'swift' }> {
  /** A natively-backed, empty, open TSB1 buffer with room for `capacity` bytes. */
  createBuffer(capacity: number): ArrayBuffer
  /** Appends `bytes` (overflowing the capacity sets the status to overflow). */
  append(buffer: ArrayBuffer, bytes: ArrayBuffer): void
  /** 1 done, 2 error, 3 cancelled, 4 overflow; only the first final status sticks. */
  setStatus(buffer: ArrayBuffer, status: number): void
  /**
   * Appends `bytes` in `chunk`-byte pieces every `intervalMs` on a background queue, without JS,
   * then sets `finalStatus` (0 leaves it open).
   */
  startProducer(buffer: ArrayBuffer, bytes: ArrayBuffer, chunk: number, intervalMs: number, finalStatus: number): void
  /** Stops every producer. */
  stopProducers(): void
  /** The view whose `nativeID` is `key` and that has `recordDisplayLog`; rejects if there is none. */
  snapshot(key: string): Promise<StreamTextSnapshot>
}
