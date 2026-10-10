# react-native-text-stream (prototype)

Streams text from a native producer into a native view without JS on the data path.

## Minimal example

```tsx
import { useState } from 'react';
import { Button } from 'react-native';
import { fetch } from 'react-native-nitro-fetch'; // patched
import { StreamText } from 'react-native-text-stream';

export function Chat() {
  const [buffer, setBuffer] = useState<ArrayBuffer>();

  const ask = async () => {
    // The response body is written natively into `nativeBuffer` as it arrives.
    const res = await fetch('https://example.com/stream', { stream: true, nativeBuffer: true });
    setBuffer(res.nativeBuffer); // the only time JS touches the stream
  };

  return (
    <>
      <Button title="Ask" onPress={ask} />
      <StreamText source={buffer} style={{ flex: 1 }} />
    </>
  );
}
```

No `setState` per chunk and no re-renders: the view reads new bytes from the buffer on every frame,
even while the JS thread is busy.

Or skip the one `setState` too and hand the buffer over through the ref (the demo screen does this):

```tsx
import { callback } from 'react-native-nitro-modules';
import { StreamText, type StreamTextRef } from 'react-native-text-stream';

const view = useRef<StreamTextRef | null>(null);

const res = await fetch(url, { stream: true, nativeBuffer: true });
view.current?.attachSource(res.nativeBuffer); // attachSource(undefined) clears

<StreamText hybridRef={callback((ref: StreamTextRef) => (view.current = ref))} style={{ flex: 1 }} />
```

Both work; whichever was set last (`source` prop or `attachSource`) wins.

`res.nativeBuffer` is a Nitro-owned `ArrayBuffer` in the **TSB1** layout: a 64-byte header
(`magic`, `version`, `committed`, `capacity`, `status`) followed by an append-only byte log
(`packages/react-native-text-stream/ios/TextStreamBuffer.h`). URLSession appends to it on its own
queue (store-release of `committed`); `StreamText` reads new bytes once per frame on the main thread
(load-acquire), cuts at UTF-8 boundaries and appends them to a TextKit 2 `UITextView`.

iOS only for now.

## Layout

- `packages/react-native-text-stream` — Nitro HybridView `StreamText` (`source`, `fontSize`, `showStats`).
- `packages/react-native-nitro-fetch` — vendored nitro-fetch 1.8.0 (`aad957e`) plus the `nativeBuffer` patch,
  also exported as `patches/react-native-nitro-fetch+native-buffer.patch`.
- `patches/*@*.patch` — applied on `postinstall` by `scripts/apply-patches.sh`: upstream nitro-fetch's
  nitrogen fix, and the Metro patch that worklets Bundle Mode (react-native-streamdown) needs.
- `server/stream-server.mjs` — local streaming server (`fmt=raw|sse|ndjson`, `tps`, `chunk`,
  `repeat|bytes`), with a per-run send log (`/log?id=`) and a results sink (`POST /results`).
- `example/src/bench` — the benchmark screen (**Benchmarks ›** in the demo header).

## Run

```sh
npm install
cd example && npm run pod
node ../server/stream-server.mjs   # http://localhost:9877
npm start                          # Metro on 8081
npm run ios
```

The example screen streams the same response into two cards at once: **StreamText** (the patched
native path) and a **JS baseline** (stock nitro-fetch `ReadableStream` → UTF-8 decode → `setState`
→ `<Text>`, one JS callback per chunk). **Freeze JS · 5s** busy-waits the JS thread: the JS card
and the JS-thread pulse stop, while the native card and the UI-thread pulse keep going.

Demo recording: `media/native-text-streaming-demo.mp4`.

## Benchmark

Five ways to render the same stream (plain text, 14 pt, same box):

| | Variant | How text reaches the screen |
|---|---|---|
| A1 | JS, naive | `ReadableStream` → `TextDecoder` (simdutf) → `setState` per chunk → `<Text>` |
| A2 | JS, best practice | same, one `setState` per frame |
| B1 | Reanimated "ReText" | JS writes a shared value → `TextInput.text` on the UI thread, no React render |
| B2 | react-native-streamdown | `setState` per frame → remend on a worklet → enriched-markdown view |
| C | this repo | `nativeBuffer` → `StreamText`, no JS per chunk |

Metrics, medians of 3 runs, Release build, iPhone 17 Pro Max **simulator** (device numbers pending):
- **latency** — byte written by the server → first frame after the main thread finished drawing it
  (server send log as the clock; the simulator and host share `Date.now()`);
- **hitches** — UI frames over 1.5× budget, monitored until the UI is smooth again (10 on-time frames);
- **UI max** — the longest UI frame.

| Scenario | A1 | A2 | B1 | B2 | C |
|---|---|---|---|---|---|
| **S1** 5 KB @ 0.8 KB/s — latency p50 / p95 | 13 / 47 ms | 29 / 61 ms | 29 / 36 ms² | 43 / 50 ms | 26 / 33 ms |
| S1 — hitches · UI max | 67 · 56 ms | 65 · 70 ms | 0 · 17 ms | 0 · 17 ms | 0 · 17 ms |
| **S2** 100 KB @ 20 KB/s — latency p50 / p95 | 187 / 702 ms | 180 / 691 ms | 30 / 38 ms² | 126 / 255 ms | 26 / 33 ms |
| S2 — hitches · UI max | 24 · 861 ms | 23 · 886 ms | 0 · 17 ms | 65 · 171 ms | 0 · 17 ms |
| **S3** 1 MB burst — shown · UI smooth again | blank¹ | blank¹ | 79 · 112 ms³ | 38 · 3219 ms | 100 · 132 ms |
| S3 — UI max | — | — | 46 ms | 1695 ms | 34 ms |
| **S4** S2 + 40 ms JS task every 100 ms — latency p50 / p95 | 182 / 722 ms | 183 / 722 ms | 49 / 81 ms² | 143 / 320 ms | **26 / 34 ms** |
| S4 — hitches · UI max | 23 · 889 ms | 23 · 911 ms | 38 · 44 ms | 63 · 180 ms | **0 · 17 ms** |
| **S5** 64 B every 400 ms — latency p50 / p95 | 19 / 49 ms | 34 / 63 ms | 29 / 35 ms² | 45 / 50 ms | 36 / 44 ms⁴ |
| S5 — hitches · UI max | 14 · 42 ms | 18 · 40 ms | 0 · 17 ms | 0 · 17 ms | 0 · 17 ms |

¹ In this setup (simulator), RN `<Text>` holding 1 MB inside a `ScrollView` rendered nothing on screen,
so A1/A2 S3 times measure nothing.
² B1 applies text in a Reanimated frame callback, so "shown" is taken one frame later than the update's
first frame. Measured one frame earlier (a lower bound), B1 is 13 / 21 ms in S2 and 19 / 51 ms in S4
(`bench-2026-10-10T21-01-00-064Z.json`): B1 and C are within a frame of each other on latency.
³ B1 doesn't scroll to follow the tail; C does.
⁴ Every chunk arrives after StreamText has gone idle, so it pays the idle poll (≤ 33 ms) on top.

Takeaways:
- At LLM pace (S1) everything keeps up on latency; the JS variants that re-render `<Text>` jank (50+ hitches).
- As the text grows (S2), the `setState` paths reach 0.7 s p95 and ~900 ms frames; B1 (Reanimated) and C
  stay smooth and within a frame of each other.
- With a busy JS thread (S4), C is unaffected (p95 34 ms, no hitches); every JS-driven path degrades,
  B1 included (40+ hitches).
- A 1 MB burst (S3) is spread over frames by C's adaptive budget: worst frame 34 ms, shown in 100 ms.
- B2 also parses markdown, which the others don't; it is the richest renderer, not a like-for-like plain-text one.

### StreamText frame loop

- **Per frame:** load `status`, then `committed` (acquire); nothing else if no new bytes.
- **Adaptive budget:** at most `bytesPerTick` per frame, re-estimated from the measured append + layout
  cost so the frame's work stays around 6 ms; a burst is spread over frames.
- **Idle:** after 250 ms without data the display link pauses; a timer polls the header every 33 ms,
  backing off to 100 ms after 2 s (a model "thinking"). The display link is capped at 60 Hz.
- **Decoding:** straight from the buffer into an `NSString`; only a UTF-8 sequence split across chunks
  (≤ 3 bytes) is copied aside. The stats pill updates only when its numbers change.
- **Accessibility:** only the last 2000 characters are exposed, so an accessibility snapshot of a long
  stream stays cheap (~0.3 s instead of ~10 s with 1 MB of text).

Caveats: simulator timing (frame pacing, no thermal limits); B2's own async work may land after its
mark, so its latency is a lower bound. **Don't drive the UI with accessibility tools during a run**:
an accessibility snapshot of a view holding megabytes of text can stall the main thread for seconds.

Results: `bench-results/bench-2026-10-10T21-36-51-079Z.json` (the table above) and
`bench-2026-10-10T21-01-00-064Z.json` (B1 single-frame check).

Run it: build **Release** (`-configuration Release`, no Metro needed), start the stream server, open
**Benchmarks ›**, tap **Run all**; results land in `bench-results/`. `FOCUS` in `example/src/bench/config.ts`
narrows a run.
