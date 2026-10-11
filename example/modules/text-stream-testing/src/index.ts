// Example app only: benchmark and test helpers, kept out of the react-native-text-stream package.
import { NitroModules } from 'react-native-nitro-modules'
import type { FrameMonitor } from './specs/frame-monitor.nitro'
import type { StreamTextTesting } from './specs/testing.nitro'

/** UI-thread frame pacing monitor for benchmarks. */
export function createFrameMonitor(): FrameMonitor {
  return NitroModules.createHybridObject<FrameMonitor>('FrameMonitor')
}

export function createStreamTextTesting(): StreamTextTesting {
  return NitroModules.createHybridObject<StreamTextTesting>('StreamTextTesting')
}

export type { FrameMonitor, FrameStats } from './specs/frame-monitor.nitro'
export type { StreamTextSnapshot, StreamTextTestStyle, StreamTextTesting } from './specs/testing.nitro'
export { bufferText, committedBytes, headerStatus } from '../../../../packages/react-native-text-stream/src/buffer'
