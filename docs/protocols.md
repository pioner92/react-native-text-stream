# Native SSE and NDJSON parsing

`streamFetch` can turn protocol records into UTF-8 text on the native network path.
The same C++ parser runs on iOS and Android. JavaScript starts the request and receives
response/completion metadata; it does not parse or forward individual tokens.
Omitting `parser` keeps the original raw-body path without constructing a protocol parser.

```tsx
const request = {
  url: 'https://your-backend.example/chat',
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ prompt: 'Explain streaming' }),
  parser: { provider: 'openai-chat' as const },
};

<StreamText request={request} format="markdown" markdownFlavor="gfm" />
```

The backend must return the selected provider's streaming format. Parser selection does
not add a request body, authentication, a `stream` flag, or an `Accept` header. Configure
those for your endpoint. The imperative `streamFetch(url, { parser, ... })` API accepts
the same settings.

## Provider presets

| `provider` | Framing | Text | Completion |
| --- | --- | --- | --- |
| `openai-chat` | SSE | Choice index 0, `delta.content` / `delta.refusal` | `[DONE]` |
| `openai-responses` | SSE | `response.output_text.delta` / `response.refusal.delta` | `response.completed` |
| `anthropic` | SSE | Initial text blocks and `text_delta` events | `message_stop` |
| `ollama` | NDJSON | `message.content` or `response` | `done: true` |

Only text deltas reach the buffer. Tool calls, reasoning/thinking, usage and full-response
snapshots are ignored. Ollama's final record may contribute text before completion.
Provider error events finish as `error`; Responses failed/incomplete events also fail.
EOF before a provider completion marker is an error, preserving text already received.
A completion marker finishes immediately and closes the request even if the server keeps
the connection open. There is no automatic reconnect, replay or retry.

## Custom endpoints

```ts
// Strip SSE framing and concatenate data payloads.
streamFetch(url, { parser: { format: 'sse' } });

// Parse each SSE data payload as JSON and select a string.
streamFetch(url, { parser: { format: 'sse', textPath: '/delta/text' } });

// Parse one JSON record per line.
streamFetch(url, { parser: { format: 'ndjson', textPath: '/message/content' } });
```

`textPath` is an RFC 6901 JSON Pointer: `/items/0/text` selects an array item,
`~1` escapes `/`, `~0` escapes `~`, and `""` selects a root JSON string.
Missing or null fields are skipped; a selected non-string is an error. NDJSON requires
`textPath`; raw SSE can omit it. A provider preset cannot be combined with `format`
or `textPath`.

SSE accepts a leading UTF-8 BOM and CR, LF or CRLF line endings, including boundaries
split across network reads. Consecutive `data:` lines join with a newline; events concatenate
without an extra separator. Comments and non-data fields do not become output.
An unfinished SSE event at EOF is discarded. NDJSON skips blank lines and accepts a complete
final JSON record without a trailing newline. Invalid JSON is an error. JSON escapes,
embedded NUL, Unicode and surrogate pairs are decoded before the text reaches the buffer.

## Limits and lifecycle

`maxEventBytes` defaults to 1 MiB and accepts integers from 1 through 16 MiB. It bounds
individual pending protocol lines/records and accumulated SSE data; exceeding it fails the
request. It is independent of output `capacity`, which counts extracted UTF-8 bytes.
The JSON parser reuses a pool that grows with the largest JSON record. Its conservative
bound is about 13 times the record size plus 256 bytes; `maxEventBytes` is a wire-record
limit, not a total memory limit. A 1 MiB JSON record can reserve about 13 MiB of parser
pool, and the optional 16 MiB limit can reserve about 208 MiB. Choose a smaller limit for
endpoints with small deltas or many concurrent streams.

Metadata therefore does not use the output budget. Overflow preserves the fitting prefix
and ends as `truncated`; a partial final UTF-8 sequence follows the existing replacement
character behavior. Exactly filling capacity can still finish as `done`.

With a parser, HTTP non-2xx responses deliver `response` metadata and then finish as `error`.
Without a parser, the original behavior remains: HTTP error bodies are readable. Cancellation
and network failures preserve committed text and do not flush a partial record. Parser options
participate in declarative request identity, including named streams.

## Verification

- `bun run test:protocol` runs shared C++ protocol and provider tests.
- `sh scripts/test-response-parser-native.sh --sanitize` runs ASan/UBSan coverage.
- `sh scripts/test-response-parser-native.sh --bench` measures the same fixture with
  1-byte, 64-byte and 8192-byte chunks on the host; this is not a mobile device benchmark.
- `bun run test:native` covers publication/lifecycle and, on macOS, iOS transport seams.
- `server/protocol-server.mjs` plus `example/__tests__/protocol.harness.tsx` exercises
  real native networking, renderer integration, cancellation and blocked-JS progress.
  The fetch group of `scripts/test-stream-fetch-harness.sh` includes these tests.

Protocol references: [WHATWG SSE](https://html.spec.whatwg.org/multipage/server-sent-events.html),
[NDJSON specification](https://github.com/ndjson/ndjson-spec),
[OpenAI streaming events](https://developers.openai.com/api/reference/resources/responses/streaming-events),
[Anthropic streaming](https://platform.claude.com/docs/en/build-with-claude/streaming),
and [Ollama streaming](https://docs.ollama.com/api/streaming).

## Validation recorded on 2026-10-15

- 153 package JS tests and 10 example JS tests passed; package/example TypeScript and package build passed.
- 1,527 shared native parser scenarios passed under ASan/UBSan, including 623 every-boundary two-chunk cuts.
- Native buffer/transport lifecycle tests passed, including iOS transport seams under ASan/UBSan.
- 34 Android JVM tests passed.
- iOS Simulator: 79 protocol/fetch/regression cases passed, followed by 48 final protocol/Markdown cases.
- Android API 35 emulator: 97 final protocol/fetch/Markdown/regression cases passed.
- iOS Debug/Release and Android Debug builds passed; published-package dry run includes parser sources and vendor licenses.

These runs used the dedicated `StreamText Markdown` iOS simulator and
`StreamText_Markdown_Tests` Android emulator. Physical-device performance was not measured.

## Recorded host measurements

On the macOS arm64 host with Apple clang 21 and `-O3`, five runs of the same
10,000-record OpenAI Chat fixture produced these medians:

| Network-fragment size simulated | Wire throughput | First extracted text |
| --- | --- | --- |
| 1 byte | 100.7 MB/s | 0.001334 ms |
| 64 bytes | 484.2 MB/s | 0.001750 ms |
| 8192 bytes | 540.0 MB/s | 0.000500 ms |

Each run verified 50,000 extracted bytes and terminal completion. These numbers
measure the parser alone, with warm code and shared host load; they exclude
network latency, JNI, UI layout and mobile-device performance. They are not a
before/after speedup claim. [Raw runs and environment](benchmarks/protocol-parser-host-2026-10-15.json).
