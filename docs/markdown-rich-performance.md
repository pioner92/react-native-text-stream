# Rich Markdown performance evidence

Run `python3 scripts/benchmark-markdown-rich-host.py` to reproduce the host measurements.
It compiles both revisions with `-O3`, warms each parser five times, then records five batches
of twenty snapshots per case. Retained-parser and fresh-parser results are separate; a fresh
parser is not a cold process.

The latest host parser evidence compares baseline `964ee45` with the streaming-polish parser
using the same rich source families. It measures native parsing only, separately reporting
exact-final and progressive snapshots. It is not a universal speedup claim.
See [host JSON](benchmarks/markdown-rich-parser-host-2026-10-15.json).

The iOS evidence contains 36 Release arm64 simulator runs on a dedicated iPhone 17 Pro Max,
iOS 26.5 simulator. It records the same source scenarios, but plain reference rendering has
different visual semantics from rich rendering. Images are excluded from the timed workload.
The values are simulator frame/settling observations, not physical-device results.
Use `example/index.markdown-rich-bench.js` as the Release entry point and run
`node scripts/markdown-bench-collector.mjs` before launch. The source is produced natively
in 512-byte chunks every 4 ms, with 40 ms of competing JavaScript work every 100 ms.
Cold/warm mount cases refer to the harness clearing/reusing the view pool and prepared cache;
rich attachments deliberately bypass the prepared-layout cache.

`statusMs` includes producer pacing and JavaScript event delivery; it is not TTFT or pure
rendering latency. `settledMs` marks the beginning of the next three on-time-frame streak
reported by the native monitor, not promise-resolution time. Frame counters come from
CADisplayLink on this simulator. UI inspection happened after timing.

The earlier rich-Markdown build (including the transparent native table attachment) produced these medians
across three repetitions. The last column totals native hitch counters across those runs.

| Rich workload | Status ms | Settling marker ms | Maximum frame ms, median | Hitches, sum |
| --- | ---: | ---: | ---: | ---: |
| 5 KB stream | 63.00 | 67.09 | 16.67 | 0 |
| 100 KB stream | 857.00 | 867.01 | 16.67 | 0 |
| One finished 5 KB message, cold | 19.00 | 52.10 | 33.33 | 2 |
| One finished 5 KB message, warm | 19.00 | 33.76 | 16.67 | 1 |
| 20 finished 5 KB messages, cold | 118.00 | 150.37 | 89.42 | 5 |
| 20 finished 5 KB messages, warm | 98.00 | 100.67 | 42.74 | 5 |

Bulk mounting still has measurable hitches. The measurements do not support a universal
maximum-performance claim. The raw artifact includes the plain overhead-reference cases,
phase counters, display logs and process memory: [iOS simulator JSON](benchmarks/markdown-rich-ios-release-2026-10-15.json).

The subsequent streaming-polish pass and its new measurements are tracked in
[streaming polish validation](markdown-streaming-polish.md).
