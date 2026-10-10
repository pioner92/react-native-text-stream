import type { HybridObject } from 'react-native-nitro-modules'

export interface FrameStats {
  /** Frames presented while monitoring. */
  frames: number
  /** Frames that took longer than 1.5x the display's frame budget. */
  hitches: number
  /** Frames the display skipped, summed over all hitches. */
  droppedFrames: number
  maxFrameMs: number
  durationMs: number
}

/** Measures UI-thread frame pacing with a CADisplayLink (benchmarks only). */
export interface FrameMonitor extends HybridObject<{ ios: 'swift' }> {
  start(): void
  /** Resolves on the next frame, so a main-thread stall in progress is included. */
  stop(): Promise<FrameStats>
  /**
   * Wall-clock ms of the first frame after the main thread has drained the work
   * queued so far (e.g. mounting a commit that just happened). Needs `start()`.
   */
  markPainted(): Promise<number>
  /**
   * Wall-clock ms at which the UI became smooth again: the first of `frames`
   * consecutive on-time frames. Catches deferred UIKit work that `markPainted`
   * misses (e.g. layout a view performs a few frames after an update). Needs `start()`.
   */
  waitIdle(frames: number): Promise<number>
}
