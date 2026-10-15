# Native Markdown parser benchmark

This is host-side evidence for the shared MD4C core. It measures parsing and
document materialization only; it does not measure React Native, JNI, Swift,
TextKit, Android layout, network delivery, or a physical mobile device.

## Method

Run:

```sh
sh scripts/test-markdown-native.sh --bench
```

The harness builds the vendored MD4C and `StreamMarkdown.cpp` with `-O3`. It
uses exact 5,120-byte and 102,400-byte GFM inputs made of list, emphasis, link,
entity, and code constructs. The fixed-size figures parse the whole document
100 times. The stream figures parse every growing prefix with 17, 128, or 1,024
new source bytes, then report per-parse latency percentiles, total CPU time,
and the total source bytes reparsed divided by final input size.

## 2026-10-15 host run

Host: Apple silicon (`arm64`), macOS 26.6.2 (25G83), Apple clang 21.0.0.

| Final input | Parser lifecycle | Throughput |
| --- | --- | ---: |
| 5 KB | retained parser | 88.5 MB/s |
| 5 KB | create/destroy per parse | 84.7 MB/s |
| 100 KB | retained parser | 90.9 MB/s |
| 100 KB | create/destroy per parse | 93.9 MB/s |

| Final input | Chunk | Parses | Total CPU | p50 / p95 / p99 | Reparsed-source amplification |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 KB | 17 B | 302 | 8.64 ms | 0.028 / 0.055 / 0.061 ms | 151.9x |
| 5 KB | 128 B | 40 | 1.16 ms | 0.030 / 0.053 / 0.054 ms | 20.5x |
| 5 KB | 1 KB | 5 | 0.167 ms | 0.033 / 0.044 / 0.044 ms | 3.0x |
| 100 KB | 17 B | 6,024 | 3.47 s | 0.568 / 1.087 / 1.186 ms | 3012.7x |
| 100 KB | 128 B | 800 | 453 ms | 0.554 / 1.066 / 1.115 ms | 400.5x |
| 100 KB | 1 KB | 100 | 57.3 ms | 0.572 / 1.092 / 1.211 ms | 50.5x |

Full snapshots are the correctness baseline: reference links and unfinished
delimiters can restyle earlier output. The amplification figures show why a
renderer must coalesce source updates and parse off its UI thread. They do not
establish an iOS or Android frame-time claim.

The same source test also passed AddressSanitizer and UndefinedBehaviorSanitizer
with malformed UTF-8, overflowing numeric entities, deep quote nesting, all
stream prefixes, BOM handling, and exact UTF-16 diff coverage.

## 2026-10-15 iOS Simulator Release harness

Raw results: [markdown-ios-simulator-2026-10-15.json](benchmarks/markdown-ios-simulator-2026-10-15.json).
The 36-run artifact records three repetitions on a dedicated arm64 iPhone 17 Pro
Max simulator running iOS 26.5, with a Release build on Apple silicon. Streaming
uses a native producer (512-byte chunks every 4 ms) and deliberate JS contention
(a 40 ms loop every 100 ms).

| Markdown stream target | Actual source bytes | `markdown.apply` total / applications, range across 3 reps | `statusMs`, range |
| --- | ---: | ---: | ---: |
| 5 KB | 5,211 | 9.58–12.58 ms / 4 | 60–61 ms |
| 100 KB | 102,411 | 119.33–122.55 ms / 49–50 | 824–830 ms |

The 20-message, finished-Markdown cache stage reported 78–93 ms with a cold
cache and 17–19 ms with the prepared cache already populated. Those are
harness status values for different cache states, not a cross-layout speedup
claim.

`statusMs` includes producer pacing and JS event-loop delay, so it is neither
renderer-only latency nor TTFT. `settledMs` is safe to retain as a post-status
monitor marker: `waitIdle(3)` resets the frame streak when called. Its returned
timestamp is the beginning of the subsequent three on-time-frame streak, not
the promise-resolution time, so it is not tabulated as a duration here. The raw
CADisplayLink frame counters are simulator data and are not physical-device
frame evidence.
