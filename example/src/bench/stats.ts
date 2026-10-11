/** [wallClockMs, cumulativeBytes] */
export type Point = [number, number];

export interface LatencyStats {
  /** Every byte was shown; otherwise the latencies are null rather than those of the head only. */
  complete: boolean;
  ttfgMs: number | null;
  completionMs: number | null;
  latP50Ms: number | null;
  latP95Ms: number | null;
  latMaxMs: number | null;
}

/**
 * Latency of byte L = time it was first shown - time the server wrote it.
 * `sent` comes from the server's send log, `shown` from the renderer.
 */
export function latencyStats(sent: Point[], shown: Point[], totalBytes: number): LatencyStats {
  const none = { latP50Ms: null, latP95Ms: null, latMaxMs: null };
  if (!sent.length || !shown.length) return { complete: false, ttfgMs: null, completionMs: null, ...none };

  const firstSent = sent[0][0];
  const ttfgMs = shown[0][0] - firstSent;
  const fullyShown = shown.find(([, b]) => b >= totalBytes);
  if (!fullyShown) return { complete: false, ttfgMs, completionMs: null, ...none };

  const latencies: number[] = [];
  let j = 0;
  for (const [tSent, bytes] of sent) {
    while (j < shown.length && shown[j][1] < bytes) j++;
    if (j === shown.length) break; // `totalBytes` below what was sent (framed formats have none)
    latencies.push(Math.max(0, shown[j][0] - tSent));
  }
  return {
    complete: true,
    ttfgMs,
    completionMs: fullyShown[0] - firstSent,
    latP50Ms: percentile(latencies, 0.5),
    latP95Ms: percentile(latencies, 0.95),
    latMaxMs: Math.max(...latencies),
  };
}

/** Linear interpolation between the closest ranks (numpy's default). */
export function percentile(values: number[], p: number): number | null {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const rank = p * (sorted.length - 1);
  const lower = Math.floor(rank);
  const upper = Math.min(lower + 1, sorted.length - 1);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (rank - lower);
}

export function median(values: (number | null)[]): number | null {
  return percentile(values.filter((v): v is number => v != null), 0.5);
}

export function pairs(flat: number[]): Point[] {
  const out: Point[] = [];
  for (let i = 0; i + 1 < flat.length; i += 2) out.push([flat[i], flat[i + 1]]);
  return out;
}
