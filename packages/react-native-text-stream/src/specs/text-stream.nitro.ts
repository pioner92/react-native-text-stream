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
  source?: ArrayBuffer
  fontSize?: number
  /** Text color as "#RRGGBB" or "#RRGGBBAA". Defaults to the system label color. */
  color?: string
  /** Shows a native "status · bytes" pill, updated without JS. */
  showStats?: boolean
}

export interface TextStreamMethods extends HybridViewMethods {}

export type TextStream = HybridView<
  TextStreamProps,
  TextStreamMethods,
  { ios: 'swift' }
>
