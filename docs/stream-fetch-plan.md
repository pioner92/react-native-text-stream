# Stream fetcher architecture and remaining work

Updated 2026-10-13 after removing the legacy external fetch integration.

`streamFetch` is the only built-in network path. It starts immediately, writes response bytes into a
native TSB1 buffer on the network thread, and returns a `TextStream` that a `StreamText` view can render
without JavaScript work per chunk. `<StreamText request={…}>` is declarative sugar over the same path.

## Current contract

- iOS uses URLSession; Android uses the app's OkHttp stack.
- The supported request subset is URL, method, headers, UTF-8 string body, timeout, `AbortSignal`,
  capacity and optional `streamId`.
- `response` resolves after response headers; `done` resolves as `done`, `error`, `cancelled` or
  `truncated`; `abort()` keeps the committed prefix.
- A `streamId` lets a request and a virtualized view meet through the native registry in either order.
- TSB1 is a public native-producer contract, but JavaScript-created `ArrayBuffer`s are deliberately not
  accepted as producers.

## Remaining work

1. Parse SSE and NDJSON natively so only application text reaches the buffer. Add OpenAI, Anthropic and
   Ollama presets after defining their error and reconnect behaviour.
2. Add a safe JS ingestion API for WebSockets, `ReadableStream` and local models. It must preserve the
   TSB1 single-writer and publication rules.
3. Decide whether native requests should appear in developer network tooling.
4. Define an application-level reconnect strategy for backgrounded or interrupted long-lived streams.
5. Extend device coverage for redirects, cookie/interceptor behaviour and physical iPhone streaming.
