/** Measures JS-thread responsiveness from timer drift. */
export function startJsProbe() {
  const intervalMs = 16;
  let last = Date.now();
  let longTasks = 0;
  let maxGapMs = 0;
  const id = setInterval(() => {
    const now = Date.now();
    const gap = now - last;
    last = now;
    maxGapMs = Math.max(maxGapMs, gap);
    if (gap > 50) longTasks++;
  }, intervalMs);
  return () => {
    clearInterval(id);
    return { jsLongTasks: longTasks, jsMaxGapMs: maxGapMs };
  };
}

/** Simulates a busy app: blocks the JS thread for `busyMs` every `everyMs`. */
export function startJsLoad(busyMs: number, everyMs: number) {
  const id = setInterval(() => {
    const until = Date.now() + busyMs;
    while (Date.now() < until) {
      // busy-wait on purpose
    }
  }, everyMs);
  return () => clearInterval(id);
}
