import {
  getHostComponent,
  NitroModules,
  type HybridRef,
} from 'react-native-nitro-modules'
import TextStreamConfig from '../nitrogen/generated/shared/json/TextStreamConfig.json'
import type {
  TextStreamProps,
  TextStreamMethods,
} from './specs/text-stream.nitro'
import type { FrameMonitor } from './specs/frame-monitor.nitro'

export const StreamText = getHostComponent<TextStreamProps, TextStreamMethods>(
  'TextStream',
  () => TextStreamConfig
)

export type StreamTextProps = TextStreamProps
export type StreamTextRef = HybridRef<TextStreamProps, TextStreamMethods>

/** Benchmark helper: UI-thread frame pacing monitor. */
export function createFrameMonitor(): FrameMonitor {
  return NitroModules.createHybridObject<FrameMonitor>('FrameMonitor')
}

export type { FrameMonitor, FrameStats } from './specs/frame-monitor.nitro'
