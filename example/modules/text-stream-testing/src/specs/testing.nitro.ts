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
  /** "canvas" (a prepared layout, no text view), "textView", or "" before the first text. */
  mode: string
  /** The canvas's engine: "coreText" or "textKit"; "" without a canvas. */
  engine: string
  /** Image tiles the canvas holds. */
  tiles: number
}

/** A text style for `compareEngines`. */
export interface StreamTextTestStyle {
  fontSize: number
  lineHeight: number
  fontFamily?: string
  fontWeight?: string
  letterSpacing?: number
  textAlign?: string
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
  /** Empties the pool of reusable text views, so the next mounts create theirs (a cold start). */
  drainTextViewPool(): void
  /** Off: finished sources are laid out on the main thread when mounted, as before prepared layouts. */
  setPreparedLayout(enabled: boolean): void
  /** False: prepared layouts use TextKit even where CoreText could. */
  setPreparedCoreText(enabled: boolean): void
  /**
   * Sets the width of the surface holding views `keys`, on the main thread as a rotation or Split
   * View does (0 restores the window's), then records what the views show, frame by frame, for `ms`:
   * JSON `{ mainMs, layouts, refused, longestMs, views: { [key]: [{ w, h, mode }] } }`, the first
   * entry being the first frame after the resize and each next one a change. `layouts`, `refused`
   * and `longestMs`: prepared layouts made on the main thread during the resize, refused for the
   * budget, and the longest of them.
   */
  resizeSurface(keys: string[], width: number, ms: number): Promise<string>
  /**
   * The canvases' image tiles, as JSON: `{ bytes, tiles, canvases, offWindow, footprintMB }`
   * (`offWindow`: canvases out of a window that still hold tiles; `footprintMB`: the app's memory).
   */
  tileMemory(): Promise<string>
  /**
   * Shows (or dismisses) a full-screen view controller over the app, which takes the app's views
   * out of the window, as a pushed screen or another tab does. Resolves with the main-thread ms.
   */
  cover(shown: boolean): Promise<number>
  /** Forgets every prepared layout, so the next mounts lay out from scratch (a cold start). */
  clearPreparedLayouts(): void
  /** Prepared layouts cached for later mounts. */
  preparedCacheCount(): number
  /** Debugging: the cached layouts, one line each. */
  preparedCacheDump(): string
  /** Posts a memory warning, as the system would. */
  simulateMemoryWarning(): void
  /**
   * How far the CoreText engine's lines are from TextKit's for `text` at `width`: line ranges
   * (`mismatchedLines`), baselines (`maxBaselineDelta`), glyph x (`maxGlyphDx`), heights.
   * `coreText` is 0 when the CoreText engine doesn't take this text.
   */
  compareEngines(text: string, style: StreamTextTestStyle, width: number): Record<string, number>
  /**
   * What showing `text` at `width` costs on the main thread, step by step (ms): the way the
   * renderer does it today, a text view around a layout prepared ahead, and drawing a prepared
   * layout without a text view.
   */
  measureMount(text: string, width: number): Promise<Record<string, number>>
  /**
   * Selects the word at (`x`, `y`) in the view's coordinates, through the same text input the
   * system's selection uses, and returns the selected text (copied too). The view must stay as is.
   */
  selectWordAt(key: string, x: number, y: number): Promise<string>
  /**
   * The selection geometry the system's selection UI reads for `length` characters from `location`:
   * JSON `{ rects: [{ x, y, w, h, rtl, start, end }], startCaret, endCaret: { x, y, h }, rtl }`.
   */
  selectionGeometry(key: string, location: number, length: number): Promise<string>
  /**
   * What VoiceOver reads from a canvas, as JSON: `{ value, page, count, lines: [{ line, text, back }] }`
   * (`back`: the line number found at the middle of the line's frame).
   */
  accessibility(key: string, lines: number[]): Promise<string>
  /** The view whose `nativeID` is `key` and that has `recordDisplayLog`; rejects if there is none. */
  snapshot(key: string): Promise<StreamTextSnapshot>
}
