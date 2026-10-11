// iOS simulator reaches the host's localhost directly; host and simulator share Date.now().
export const SERVER = 'http://localhost:9877';

export type ScenarioId = 'S1' | 'S2' | 'S3' | 'S4' | 'S5' | 'S6' | 'S7';
export type VariantId = 'A1' | 'A2' | 'B1' | 'B2' | 'C';

export interface Scenario {
  id: ScenarioId;
  title: string;
  bytes: number;
  chunk: number;
  /** Chunks per second; 0 = as fast as the socket allows. */
  tps: number;
  /** Busy-waits the JS thread for `busyMs` every `everyMs` during the run. */
  jsLoad?: { busyMs: number; everyMs: number };
  /** Paragraph size in bytes (server default: one short paragraph per sentence). */
  para?: number;
}

export const SCENARIOS: Scenario[] = [
  { id: 'S1', title: 'LLM pace · 5 KB @ 0.8 KB/s', bytes: 5_000, chunk: 16, tps: 50 },
  { id: 'S2', title: 'Long answer · 100 KB @ 20 KB/s', bytes: 100_000, chunk: 256, tps: 80 },
  { id: 'S3', title: 'Burst · 1 MB, no pacing', bytes: 1_000_000, chunk: 4096, tps: 0 },
  {
    id: 'S4',
    title: 'Busy JS · S2 + 40 ms task every 100 ms',
    bytes: 100_000,
    chunk: 256,
    tps: 80,
    jsLoad: { busyMs: 40, everyMs: 100 },
  },
  // Gaps longer than StreamText's idle threshold: each chunk arrives after the view paused.
  { id: 'S5', title: 'Bursty · 64 B every 400 ms', bytes: 1_500, chunk: 64, tps: 2.5 },
  // S2 with long paragraphs: does each append re-typeset the whole current paragraph?
  { id: 'S6', title: 'S2 with 2 KB paragraphs', bytes: 100_000, chunk: 256, tps: 80, para: 2_000 },
  { id: 'S7', title: 'S2 as one paragraph', bytes: 100_000, chunk: 256, tps: 80, para: 1_000_000 },
];

export const VARIANTS: { id: VariantId; title: string }[] = [
  { id: 'A1', title: 'JS · setState per chunk' },
  { id: 'A2', title: 'JS · setState per frame' },
  { id: 'B1', title: 'Reanimated · animated TextInput' },
  { id: 'B2', title: 'react-native-streamdown · per frame' },
  { id: 'C', title: 'Native · nativeBuffer + StreamText' },
];

/** Narrows a run for investigation; leave empty for the full matrix. */
export const FOCUS: {
  scenarios?: ScenarioId[];
  variants?: VariantId[];
  reps?: number;
  /** Keeps each variant on screen this long after it finishes, for visual checks. */
  holdMs?: number;
} = { reps: 2 };

/**
 * `block`: the text sits in a plain View that sizes to it, nothing scrolls (the component alone).
 * `box`: each variant fills a fixed area and scrolls itself. `bubble`: the text sits in a
 * chat bubble that grows with it, inside a scrolling list (C uses the Fabric StreamTextView).
 */
export const LAYOUT: 'box' | 'bubble' | 'block' = 'block';

/** Goes into result file names, to tell A/B runs apart. */
export const TAG = 'current';

export const RUN_TIMEOUT_MS = 30_000;
export const COOLDOWN_MS = 1_500;

export function streamUrl(s: Scenario, runId: string) {
  const para = s.para ? `&para=${s.para}` : '';
  return `${SERVER}/?fmt=raw&bytes=${s.bytes}&chunk=${s.chunk}&tps=${s.tps}${para}&id=${encodeURIComponent(runId)}`;
}
