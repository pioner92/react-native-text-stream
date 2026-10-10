import { NitroModules } from 'react-native-nitro-modules'
import type { FrameMonitor } from './specs/frame-monitor.nitro'

export {
  StreamText,
  useStreamTextRef,
  type StreamTextProps,
  type StreamTextHandle,
  type StreamTextRef,
} from './StreamText'

/** Benchmark helper: UI-thread frame pacing monitor. */
export function createFrameMonitor(): FrameMonitor {
  return NitroModules.createHybridObject<FrameMonitor>('FrameMonitor')
}

export type { FrameMonitor, FrameStats } from './specs/frame-monitor.nitro'
