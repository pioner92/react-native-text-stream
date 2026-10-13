# Historical simulator benchmarks

[← Current benchmarks](benchmarks.md) · [README](../README.md)

> Historical results from the former README. These use older source revisions and measurement paths.
> They are not the source of the current social card. Timing markers estimate UI progress, not pixel presentation.


The historical comparison below includes an external native TSB1 producer (C), which polls its buffer,
and the library's own `streamFetch` (D), which wakes the view on every write. New work uses D. Release
build on an iPhone 17 Pro Max **simulator**; each streaming
number is the median of 2 runs, each mounting number the median of 3 cold runs. Mounting on a real
**iPhone 16 Pro** (120 Hz): [device results](../bench-results/iphone16pro-2026-10-11.md). Android, streaming
and mounting on a **Pixel 8a**: [device results](../bench-results/pixel8a-2026-10-12.md).

### TL;DR

- **Streaming:** In this historical series, StreamText had the lowest p95 delay in the measured chat-bubble scenarios: about 11–23 ms
  of delay, **0 stutters**, whether the answer is 5 KB or 100 KB, and whether the JS thread is idle or busy.
  The `setState` versions reach 0.5–1 s of delay and ~1 s frames on a long answer.
- **On its own**, with a short answer and an idle JS thread, `setState` per chunk is 2–3 ms faster; on
  everything longer, StreamText wins by 2–10×.
- **Mounting finished messages:** StreamText is on screen first and never drops a frame: 34 ms for a 50 KB
  message against 51 ms for `<Text>`, and 50 ms for 20 × 5 KB, where `<Text>` takes 134 ms and stalls for
  475 ms. On an iPhone 16 Pro its longest frame stays at 8 ms, one 120 Hz frame
  ([device results](../bench-results/iphone16pro-2026-10-11.md)).
- **Where StreamText costs more:**
  - Mounting lays the text out in the React commit, on the JS thread: 22 ms of JS for 50 KB and 38 ms for
    20 × 5 KB (`<Text>` takes 43 and 118 ms there).
  - A 1 MB burst: StreamText spreads it over frames on purpose (no long frame, but all of it is on screen
    after ~1.7 s).

### streamFetch and push

The library's own request (D, `<StreamText request>`) against C, in a chat bubble, Release, 3 runs each:
delay p50 / p95, ms.

| Scenario | iOS simulator C | iOS simulator **D** | Pixel 8a C | Pixel 8a **D** |
|---|---|---|---|---|
| Short answer at LLM pace (5 KB) | 7 / 17 | **4 / 11** | 13 / 22 | **7 / 16** |
| Long answer (100 KB) | 7 / 13 | **4 / 9** | 15 / 22 | **11 / 17** |
| A few bytes every 400 ms | 8 / 18 | **3 / 11** | 17 / 25 | **12 / 16** |
| Long answer, one 100 KB paragraph | 7 / 14 | **3 / 8** | 11 / 18 | 12 / **16** |

On the Pixel the first text is on screen 6–15 ms after the request starts instead of 24–45. Raw data:
[iOS](../bench-results/bench-bubble-push-2026-10-12T19-16-36-154Z.json),
[Pixel](../bench-results/bench-bubble-push-2026-10-12T19-24-23-243Z.json).

### Contenders

| | Contender | How the text gets on screen |
|---|---|---|
| **A1** | `setState` per chunk | JS reads the stream, `setState` on every chunk, `<Text>` re-renders |
| **A2** | `setState` per frame | the same, batched to one `setState` per frame |
| **B1** | Reanimated | JS writes a shared value, a `TextInput` shows it from the UI thread |
| **B2** | react-native-streamdown | `setState` per frame, Markdown parsed on a worklet |
| **C** | **StreamText (historical external producer)** | a native TSB1 buffer goes straight to the view; no JS per chunk |
| **D** | **StreamText, `request`** | the view's own request (`streamFetch`), waking the view on every write |

**How to read the tables**

- **Delay** is the time from the server sending the text until it is on screen. Two numbers: typical /
  worst 5% (p50 / p95). Lower is better.
- **Stutters** is how many frames were late (a frame over 1.5× its budget). **0 means no over-budget gaps were recorded by this monitor.**
- **Longest frame**: one 60 Hz frame is 17 ms, so **17 ms is consistent with the monitor recording no over-budget frame**.
- The best result in each row is in **bold**.

### 1. In a chat bubble (the main use case)

The answer streams into the last bubble of a chat list. The bubble grows with the text.

**Delay, ms (typical / worst 5%)**

| Scenario | A1 | A2 | B1 | B2 | **StreamText** |
|---|---|---|---|---|---|
| Short answer at LLM pace (5 KB) | 15 / 49 | 29 / 65 | 36 / 78 | 42 / 50 | **13 / 23** |
| Long answer (100 KB) | 205 / 708 | 186 / 741 | 566 / 1119 | 131 / 300 | **11 / 18** |
| Long answer while the JS thread is busy | 168 / 697 | 209 / 737 | 573 / 1115 | 162 / 367 | **11 / 17** |
| Long answer, 2 KB paragraphs | 169 / 568 | 148 / 550 | 396 / 783 | 190 / 503 | **11 / 17** |
| Long answer, one 100 KB paragraph | 151 / 617 | 159 / 760 | 382 / 766 | 201 / 471 | **11 / 17** |
| A few bytes every 400 ms | **14** / 36 | 27 / 52 | 35 / 48 | 44 / 51 | 16 / **21** |

**Stutters, and the longest frame**

| Scenario | A1 | A2 | B1 | B2 | **StreamText** |
|---|---|---|---|---|---|
| Short answer at LLM pace (5 KB) | 75 · 67 ms | 71 · 70 ms | 70 · 50 ms | **0 · 17 ms** | **0 · 17 ms** |
| Long answer (100 KB) | 22 · 875 ms | 23 · 996 ms | 30 · 612 ms | 66 · 161 ms | **0 · 17 ms** |
| Long answer while the JS thread is busy | 25 · 952 ms | 21 · 970 ms | 26 · 612 ms | 67 · 199 ms | **0 · 17 ms** |
| Long answer, 2 KB paragraphs | 27 · 701 ms | 29 · 681 ms | 35 · 432 ms | 52 · 344 ms | **0 · 17 ms** |
| Long answer, one 100 KB paragraph | 28 · 737 ms | 28 · 734 ms | 36 · 426 ms | 56 · 318 ms | **0 · 17 ms** |
| A few bytes every 400 ms | 14 · 38 ms | 16 · 38 ms | 17 · 99 ms | **0 · 17 ms** | **0 · 17 ms** |
| 1 MB all at once | 2 · 277 ms | **0 · 17 ms** | 4 · 5452 ms | 1 · 1949 ms | 2 · 51 ms |

<details>
<summary><b>2. On its own, outside any list</b></summary>

The text in a plain `View`, nothing scrolls: the component alone.

| Scenario | A1 | A2 | B1 | B2 | **StreamText** |
|---|---|---|---|---|---|
| Short answer at LLM pace (5 KB) | **11 / 19 ms** · 0 | 26 / 34 ms · 0 | 28 / 36 ms · 1 | 43 / 50 ms · 0 | 14 / 23 ms · **0** |
| Long answer (100 KB) | 26 / 50 ms · 0 | 31 / 77 ms · 0 | 59 / 132 ms · 66 | 115 / 244 ms · 75 | **12 / 18 ms · 0** |
| Long answer while the JS thread is busy | 13 / 46 ms · 0 | 31 / 62 ms · 0 | 54 / 119 ms · 36 | 121 / 245 ms · 59 | **11 / 18 ms · 0** |
| Long answer, one 100 KB paragraph | 65 / 188 ms · 53 | 68 / 208 ms · 53 | 100 / 187 ms · 69 | 149 / 301 ms · 69 | **12 / 18 ms · 0** |
| A few bytes every 400 ms | **12 / 19 ms** · 0 | 24 / 33 ms · 0 | 27 / 35 ms · 0 | 42 / 51 ms · 0 | 17 / 23 ms · **0** |

Cells: delay (typical / worst 5%) · stutters.

</details>

<details>
<summary><b>3. Showing a finished answer</b></summary>

How long it takes to mount text that is already complete (for example an earlier message). Each run
mounts texts nothing has laid out before. Cells: on screen / longest frame, in ms; 17 ms is one 60 Hz
frame, so **/ 17** means no frame was late.

| What is mounted | `<Text>` | `TextInput` | streamdown | **StreamText** |
|---|---|---|---|---|
| 1 message, 5 KB | **17 / 17** | **17 / 17** | 25 / 17 | **17 / 17** |
| 1 message, 50 KB | 51 / 17 | 51 / 17 | 17 / 105 | **34 / 17** |
| 20 messages, 600 B each | 34 / 104 | 90 / 130 | 31 / 47 | **33 / 17** |
| 20 messages, 5 KB each | 134 / 475 | 229 / 520 | 18 / 205 | **50 / 17** |
| JS time in the commit: 50 KB · 20 × 5 KB | 43 · 118 ms | 34 · 117 ms | **2 · 8 ms** | 22 · 38 ms |
| 20 empty bubbles, waiting for the first token | — | — | — | **17 / 17** |
| Memory for 20 messages of 5 KB¹ | 747 MB | — | 261 MB | **≈ 5 MB** |

streamdown is on screen early, but its long frames come after that. On an iPhone 16 Pro (120 Hz)
StreamText's longest frame is 8 ms in every row but 20 × 600 B (22 ms):
[device results](../bench-results/iphone16pro-2026-10-11.md).

¹ From [an earlier run](../bench-results/mount-watcher-2026-10-11T13-20-11-895Z.json); `TextInput` read as
negative (the memory reading is noisy), so it is left out.

</details>

<details>
<summary><b>4. A long chat</b></summary>

StreamText in a list of 120 finished messages (870 KB of text):

| | StreamText |
|---|---|
| Mount all 120 messages at once | 566 ms |
| Mount 120 empty bubbles | 17 ms (one frame) |
| Add one more message | shown in the same frame |
| Scroll a FlatList of them (40 000 pt) | longest frame 33 ms |
| Stream an answer below the 120 messages | **0 stutters** |

</details>

<details>
<summary><b>Methodology and raw results</b></summary>

- **Delay** of a byte: the time the stream server wrote it (its log is the clock; the simulator and the
  host share `Date.now()`) to the first frame after the main thread drew it. For JS variants that is the
  first frame after the React commit; for StreamText, the frame after its append.
- **Stutters** (hitches): UI frames longer than 1.5× the frame budget, counted until the UI is smooth again
  (10 on-time frames). **Longest frame**: the longest UI frame in the run.
- In the 1 MB burst, the JS variants' delay marks the React commit, not text on screen (a 1 MB `<Text>`
  isn't drawn by then), so only frames are compared.
- B1 shows its text one Reanimated frame later than its update. B2's own async work may land after its
  mark, so its delay is a lower bound. B2 also parses Markdown, which the others don't.
- Mounting: from `setState` to the text on screen (for StreamText, its final status), then the longest
  frame until the UI is idle; JS time is the React commit. Each run mounts texts nothing has laid out
  before, so neither StreamText's cache nor `<Text>`'s helps. Memory is the process footprint before and
  after.
- The streaming tables are simulator only (no thermal limits, the Mac's CPU): on a device, the per-byte
  delay would need a clock shared with the server. Mounting on an iPhone 16 Pro:
  [device results](../bench-results/iphone16pro-2026-10-11.md).

Raw results:
[chat bubble](../bench-results/bench-bubble-readme-2026-10-11T14-17-47-595Z.json) ·
[on its own](../bench-results/bench-block-watcher-2026-10-11T13-10-43-575Z.json) ·
[mounting](../bench-results/mount-current-2026-10-11T17-41-13-016Z.json) ·
[long chat](../bench-results/history-final2-2026-10-11T13-25-00-096Z.json).
Run them yourself from the example app: **Benchmarks ›** (Run all, Mount, History).

</details>
