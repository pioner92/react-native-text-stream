import type {
  HybridView,
  HybridViewProps,
  HybridViewMethods,
} from 'react-native-nitro-modules'

export interface TextStreamProps extends HybridViewProps {
  /**
   * A TSB1 buffer (e.g. `response.nativeBuffer` from nitro-fetch). The view
   * reads new bytes from it once per frame on the main thread, without JS.
   */
  // `null` too: Fabric sends null when a previously set prop is removed.
  source?: ArrayBuffer | null
  fontSize?: number
  /** Text color as "#RRGGBB" or "#RRGGBBAA". Defaults to the system label color. */
  color?: string
  /** Shows a native "status · bytes" pill, updated without JS. */
  showStats?: boolean
  /** Benchmarks only: record when bytes reach the screen, read with `getDisplayLog()`. */
  recordDisplayLog?: boolean
}

export interface TextStreamMethods extends HybridViewMethods {
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
