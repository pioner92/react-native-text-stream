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
  /**
   * `[wallClockMs, bytesShown, ...]` recorded by the Fabric StreamTextView whose
   * `nativeID` is `key`: when its bytes reached the screen.
   */
  streamTextDisplayLog(key: string): number[]
  /**
   * `[ticks, appendMs, layoutMs, commitMs, maxTickMs, heightReports, maxTickBytes, slowTicks, grows]`
   * main-thread cost of the StreamTextView with `nativeID` `key`; empty until its stream finished.
   */
  streamTextRenderStats(key: string): number[]
  /**
   * `[inits, initMs, ticks, tickMs, layoutMs, commitMs, attaches, attachMs, widthLayouts, widthLayoutMs]`
   * summed over every StreamTextView since the last reset. tickMs includes the layout and commit
   * done in ticks; layoutMs/commitMs also count the measuring done outside ticks; attachMs and
   * widthLayoutMs (a view's width changed) include any tick and commit they ran.
   */
  streamTextTotals(reset: boolean): number[]
  /** The app's physical memory footprint in MB (what Xcode's memory gauge shows). */
  memoryFootprintMb(): number
}
