import type {
  HybridView,
  HybridViewProps,
  HybridViewMethods,
} from 'react-native-nitro-modules'

/** Where a stream is, as seen by the view: terminal states fire once everything is shown. */
export type StreamTextStatus =
  | 'idle'
  | 'streaming'
  | 'done'
  | 'error'
  | 'cancelled'
  | 'truncated'

export type StreamTextAlign = 'auto' | 'left' | 'right' | 'center' | 'justify'

export type StreamTextIndicatorStyle = 'default' | 'black' | 'white'

export interface StreamTextInsets {
  top?: number
  left?: number
  bottom?: number
  right?: number
}

export interface StreamTextState {
  status: StreamTextStatus
  /** Bytes the producer has committed so far. */
  bytes: number
  /** Bytes the view has consumed (shown, or held as a split UTF-8 sequence). */
  shownBytes: number
}

export interface TextStreamProps extends HybridViewProps {
  /**
   * A TSB1 buffer (e.g. `response.nativeBuffer` from nitro-fetch). The view
   * reads new bytes from it once per frame on the main thread, without JS.
   */
  // `null` too: Fabric sends null when a previously set prop is removed.
  source?: ArrayBuffer | null

  fontSize?: number
  /** A font family name, e.g. "Menlo"; defaults to the system font. */
  fontFamily?: string
  /** "normal", "bold" or "100"…"900". */
  fontWeight?: string
  /** Text color as "#RRGGBB" or "#RRGGBBAA". Defaults to the system label color. */
  color?: string
  /** Line height in points. */
  lineHeight?: number
  letterSpacing?: number
  textAlign?: StreamTextAlign
  /** Padding between the scroll view's edges and the text. */
  contentInset?: StreamTextInsets

  /** Defaults to true. */
  selectable?: boolean
  /** Selection handles and highlight, "#RRGGBB" or "#RRGGBBAA". */
  selectionColor?: string
  showsVerticalScrollIndicator?: boolean
  indicatorStyle?: StreamTextIndicatorStyle

  /** Fires when the stream starts, and when it ends once all of its text is on screen. */
  onStatusChange?: (status: StreamTextStatus, bytes: number) => void
  /** Fires when the user scrolls away from the end, or back to it. */
  onAtEndChange?: (atEnd: boolean) => void
  /** Fires when the text's laid-out size changes, at most every 100 ms. */
  onContentSizeChange?: (width: number, height: number) => void

  /** Shows a native "status · bytes" pill, updated without JS. */
  showStats?: boolean
  /** Benchmarks only: record when bytes reach the screen, read with `getDisplayLog()`. */
  recordDisplayLog?: boolean
}

export interface TextStreamMethods extends HybridViewMethods {
  /**
   * Same as the `source` prop, without a React render: `ref.attachSource(res.nativeBuffer)`.
   * Whichever of the two was set last wins; `undefined` detaches.
   */
  attachSource(source?: ArrayBuffer): void
  scrollToEnd(animated: boolean): void
  /** A snapshot of the stream's state, readable synchronously from JS. */
  getStatus(): StreamTextState
  /** The text shown so far. */
  getText(): Promise<string>
  /**
   * Benchmark hook: `[wallClockMs, bytesShown, ...]` pairs, one per frame
   * that appended text since the current `source` was attached.
   */
  getDisplayLog(): number[]
}

export type TextStream = HybridView<
  TextStreamProps,
  TextStreamMethods,
  { ios: 'swift' }
>
