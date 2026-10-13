# Benchmarks

[← README](../README.md) · [Architecture](architecture.md) · [Run the example](../example/README.md)

## Current comparison · 13 October 2026

**57 completed runs:** 45 streaming runs and 12 mount runs. Every comparison uses the same
arm64 Release app, with a production JavaScript bundle, on an **iPhone 17 Pro Max simulator
running iOS 26.5**, hosted on an **Apple M3 Max with 96 GB RAM**.

Each cell below is the **median of three runs**, followed by the **minimum–maximum** in parentheses.
Times are in milliseconds. The social card rounds to whole milliseconds; the raw data keeps full precision.

![StreamText comparison: update delay, UI stutters, and mount time](../media/benchmark-card-v2/benchmark-table-focused.png)

## Streaming with busy JavaScript

Scenario S4 streams a 100 KB plain-text answer into a chat bubble: 256-byte target chunks at
80 chunks per second (~20 KB/s), plus a **40 ms JS task every 100 ms**. All implementations use
the same native bottom-anchored chat container. Monitoring continues until the UI has ten on-time frames.

| Implementation | Estimated p95 update delay, ms | UI stutters | Longest UI frame, ms |
| --- | ---: | ---: | ---: |
| **StreamText** | 9.1 (8.0–9.7) | 0 (0.0–0.0) | 17 (17.0–17.0) |
| React `<Text>` · every update | 758.5 (665.8–811.6) | 22 (22.0–22.0) | 976 (919.0–1023.0) |
| React `<Text>` · once per frame | 854.2 (584.2–887.2) | 20 (20.0–26.0) | 1067 (1051.0–1080.0) |
| Reanimated · animated TextInput | 1155.1 (1132.7–1156.3) | 30 (27.0–31.0) | 642 (631.0–643.0) |
| streamdown · Markdown | 545.9 (323.2–587.1) | 57 (55.0–64.0) | 408 (195.0–417.0) |

StreamText recorded **zero UI stutters in all three S4 runs**. Its native request path produced
zero JS chunk callbacks. These observations apply to this workload and environment.

## Mounting finished messages

The mount workload creates **20 messages × approximately 5 KB** in a 408-point-wide clipped container,
without list virtualization. Each message contains 5,012 UTF-8 bytes. Content is ready before timing
starts; network delivery is excluded. Font size is 14, with a line height of 21.

| Implementation | Mount marker, ms | Longest UI frame, ms |
| --- | ---: | ---: |
| **StreamText** | 67 (67.0–67.0) | 17 (17.0–17.0) |
| React `<Text>` | 150 (150.0–151.0) | 86 (86.0–88.0) |
| Reanimated · animated TextInput | 134 (118.0–201.0) | 139 (126.0–142.0) |
| streamdown · Markdown | 167 (55.0–184.0) | 174 (122.0–233.0) |

The two React streaming strategies use the same static mount implementation, so the card repeats
one mount value for both React rows. Reanimated uses an animated TextInput, not a plain TextInput baseline.

## Other streaming workloads

S1: 5 KB at ~0.8 KB/s. S2: 100 KB at ~20 KB/s. Neither adds the artificial JS task used in S4.

| Implementation | Short reply p95, ms | Long reply p95, ms |
| --- | ---: | ---: |
| **StreamText** | 12.5 (10.1–14.9) | 10.3 (8.7–12.2) |
| React `<Text>` · every update | 49.4 (45.0–51.1) | 786.1 (677.0–818.2) |
| React `<Text>` · once per frame | 67.5 (64.9–74.5) | 768.4 (737.4–846.6) |
| Reanimated · animated TextInput | 75 (70.3–75.4) | 1176.6 (1147.4–1284.9) |
| streamdown · Markdown | 46.4 (45.7–49.0) | 631.5 (369.0–713.6) |

## What is measured

- **Update delay:** elapsed time from a server write to the first native UI marker covering those
  bytes. p95 is calculated over send events in each run, then the median of those three p95 values
  is reported. This is a timing estimate, not a measured pixel-presentation timestamp.
- **UI stutters / hitches:** native CADisplayLink timestamp gaps greater than **1.5× the expected
  frame interval**. At 60 Hz that threshold is about 25 ms. This counts long UI frame intervals,
  not compositor-confirmed dropped frames.
- **Longest UI frame:** the largest display-link timestamp gap during the monitored interval.
- **Mount time:** from requesting the mounted tree to a native timestamp after two frame markers
  following React commit. StreamText also waits for all 20 views to report a final `done` status.
  Monitoring continues after the mount marker until the UI settles, so later stalls remain visible
  in the longest-frame column.

## How to interpret the comparison

This compares complete implementations. StreamText uses **its own native `streamFetch`**; the JS
variants use **React Native XMLHttpRequest progress**. It does not isolate renderer cost from transport
cost. The current series does not use `nitro-fetch`.

`streamdown` parses Markdown even though the benchmark input is plain text. Its asynchronous
rendering may finish after its timing marker, so its delay and mount estimates are optimistic.
One mount marker arrived at 55 ms while that run's longest frame was 174 ms. Both observations
are retained rather than treating the early marker as completion of all rendering work.

The run order rotates. Mount content differs across messages and repetitions, while every variant
receives the same text within a repetition. StreamText's prepared-layout cache is cleared before each
mount cell. No UI automation or accessibility inspection ran during timed measurements.

Clock offset is calibrated per streaming run using the fastest of 12 requests. Selected round-trip
times were **8–16 ms**. Small millisecond differences should not be used for precise
speed ratios. Other host workloads and refresh behavior were not experimentally locked. Native
benchmark helpers and installed devtools instrumentation were present.

These are **simulator results**, not physical-iPhone measurements or a guarantee of performance on
every device. Three repetitions describe this sample; they are not a population confidence interval.

## Reproduce and inspect

1. Follow the [example setup](../example/README.md) and start the local server on port 9877.
2. From the repository root, build the dedicated Release entry point. The helper requires
   Python 3 and the `rtk` command wrapper on your `PATH`:

   ```sh
   python3 scripts/build-social-bench-ios.py
   ```

3. Install the generated simulator app from
   `example/ios/build/Build/Products/Release-iphonesimulator/TextStreamExample.app`.
4. Run **Streaming → Run all**, wait for it to save results, then run **Mount → Run mount**.
   Do not inspect the UI or capture the accessibility tree during either timed suite.
5. Keep the complete result files. The script temporarily selects S1/S2/S4 × five implementations
   × three repetitions and restores the original configuration afterward.

To regenerate the card from the saved dataset:

```sh
python3 media/benchmark-card-v2/summarize.py
python3 media/benchmark-card-v2/render.py media/benchmark-card-v2/data.json --focused --out media/benchmark-card-v2/benchmark-table-focused.svg --png
```

PNG export requires `rsvg-convert`; the SVG renderer itself uses the Python standard library.

- [Raw streaming runs](../media/benchmark-card-v2/raw-streaming.json)
- [Raw mount runs](../media/benchmark-card-v2/raw-mount.json)
- [Aggregates, including all ranges](../media/benchmark-card-v2/summary.json)
- [Environment, dependency versions, and source/binary fingerprints](../media/benchmark-card-v2/environment.json)
- [Editable social card](../media/benchmark-card-v2/benchmark-table-focused.svg)

## Earlier measurements

Earlier results use different revisions, transports, or workloads. They are retained as historical
evidence and are not combined with the current comparison:

- [October 11 simulator comparison](benchmarks-archive.md)
- [iPhone 16 Pro mounting](../bench-results/iphone16pro-2026-10-11.md)
- [Pixel 8a streaming and mounting](../bench-results/pixel8a-2026-10-12.md)
