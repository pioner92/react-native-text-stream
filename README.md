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
- `patches/nitrogen@0.37.1.patch` — upstream nitro-fetch's nitrogen fix, applied on `postinstall`.
- `server/stream-server.mjs` — local streaming server (`fmt=raw|sse|ndjson`, `tps`, `chunk`, `repeat`).

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
