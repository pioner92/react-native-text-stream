/** [wallClockMs, cumulativeBytes] */
export type Point = [number, number];

export interface LatencyStats {
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
  const empty = { ttfgMs: null, completionMs: null, latP50Ms: null, latP95Ms: null, latMaxMs: null };
  if (!sent.length || !shown.length) return empty;

  const latencies: number[] = [];
  let j = 0;
  for (const [tSent, bytes] of sent) {
    while (j < shown.length && shown[j][1] < bytes) j++;
    if (j === shown.length) break; // never fully shown (timeout)
    latencies.push(Math.max(0, shown[j][0] - tSent));
  }
  const firstSent = sent[0][0];
  const fullyShown = shown.find(([, b]) => b >= totalBytes);
  return {
    ttfgMs: shown[0][0] - firstSent,
    completionMs: fullyShown ? fullyShown[0] - firstSent : null,
    latP50Ms: percentile(latencies, 0.5),
    latP95Ms: percentile(latencies, 0.95),
    latMaxMs: latencies.length ? Math.max(...latencies) : null,
  };
}

export function percentile(values: number[], p: number): number | null {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(p * sorted.length))];
}

export function median(values: (number | null)[]): number | null {
  return percentile(values.filter((v): v is number => v != null), 0.5);
}

export function pairs(flat: number[]): Point[] {
  const out: Point[] = [];
  for (let i = 0; i + 1 < flat.length; i += 2) out.push([flat[i], flat[i + 1]]);
  return out;
}
