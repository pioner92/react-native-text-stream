# API reference

[← README](../README.md) · [Usage](usage.md) · [Architecture](architecture.md)

All names are exported from `react-native-text-stream`.

## `StreamText`

```tsx
import { StreamText } from 'react-native-text-stream'
```

`StreamText` is a native plain-text view. It accepts standard React Native
`ViewProps` plus the following props.

| Prop | Type | Default | Description |
| --- | --- | --- | --- |
| `request` | `StreamTextRequest` | — | Starts a request and displays its body. With no `streamId`, the view owns it. |
| `streamId` | `string` | — | Displays the current native stream registered under this id. |
| `source` | `TextStream \| ArrayBuffer \| null` | — | An explicit `streamFetch()` result or native-owned TSB1 buffer. `null` detaches it. |
| `fontSize` | `number` | `15` | Font size in points. |
| `fontFamily` | `string` | system | Font family or PostScript name. |
| `fontWeight` | `StreamTextFontWeight` | `'normal'` | Normal, bold, or a supported numeric weight. |
| `color` | `ColorValue` | platform label color | Text color. |
| `lineHeight` | `number` | font default | Line height in points. |
| `letterSpacing` | `number` | `0` | Letter spacing. |
| `textAlign` | `StreamTextAlign` | `'auto'` | Text alignment. |
| `selectable` | `boolean` | `true` | Enables native text selection. |
| `selectionColor` | `ColorValue` | platform tint | Selection highlight color. |
| `onStatusChange` | `(status, bytes) => void` | — | Emits display lifecycle updates. |

`source` takes precedence over `streamId` while it is present. Removing `source`
returns the view to the named stream.

### `StreamTextRequest`

```ts
interface StreamTextRequest extends StreamFetchInit {
  url: string
}
```

See [`streamFetch`](#streamfetchurl-init) for request options and their
validation rules.

### `onStatusChange`

```ts
type StreamTextStatus = 'idle' | 'streaming' | 'done' | 'error' | 'cancelled' | 'truncated'
```

| Status | Meaning |
| --- | --- |
| `idle` | No valid source is attached. |
| `streaming` | The view is displaying an open stream. |
| `done` | The transport finished and the view displayed the committed text. |
| `error` | The request failed. |
| `cancelled` | The request was cancelled. |
| `truncated` | The capacity limit was reached; later response bytes were dropped. |

A finished buffer attached to a view can report only its final state. Stale events
from a stream replaced by `source`, `streamId`, or `attach()` are ignored.

## `streamFetch(url, init?)`

```ts
function streamFetch(url: string, init?: StreamFetchInit): TextStream
```

Starts a native HTTP request and returns immediately. The body is appended to a
native buffer as it arrives.

| Option | Type | Default |
| --- | --- | --- |
| `method` | `string` | `GET`, or `POST` when `body` is present |
| `headers` | record, tuples, or Headers-like | — |
| `body` | `string` | — |
| `signal` | `AbortSignal` | — |
| `capacity` | `number` | 16 MiB |
| `timeout` | `number` | 300,000 ms |
| `streamId` | `string` | — |

`GET` and `HEAD` cannot have a body. `capacity` and `timeout` must be positive
integers. Capacity is capped at 2,147,483,583 bytes; timeout is capped at
2,147,483,647 ms. The timeout runs through queueing, connection, upload, and
response headers; after headers it becomes an idle-read timeout, rather than a
total-transfer deadline. Header names must be HTTP tokens and values must be
ASCII without CR/LF. Explicit `Accept-Encoding` accepts only `identity` or
`gzip`.

Validation failures throw synchronously from `streamFetch()`. The `request` prop
uses the same validation during render, so invalid request props surface through
React's normal render error handling. A pre-aborted signal starts no request and
does not replace an existing named stream. Exceeding capacity ends as `truncated`;
exactly filling capacity followed by EOF ends as `done`.

### `TextStream`

```ts
interface TextStream {
  readonly buffer: ArrayBuffer
  readonly response: Promise<StreamFetchResponse>
  readonly done: Promise<StreamFetchResult>
  abort(): void
  text(): string
}
```

| Member | Meaning |
| --- | --- |
| `buffer` | Native TSB1 buffer, suitable for `source` or `attach()`. |
| `response` | Resolves at headers with `{ status, ok, headers, url }`; rejects if no response is received. Response header names are lower case. |
| `done` | Always resolves after the request ends. `{ status: 'error', error }` includes its error text. |
| `abort()` | Cancels this request. Already committed body bytes remain readable. |
| `text()` | Decodes the committed prefix as UTF-8. A terminal incomplete sequence becomes U+FFFD. |

HTTP status codes do not turn into transport errors. A 4xx/5xx body remains
available; inspect `response.ok` or `response.status`.

## Named-stream controls

```ts
function abortStream(streamId: string): void
function releaseStream(streamId: string): void
```

`abortStream` stops the named request. `releaseStream` removes the named registry
entry but does not abort it or clear views already displaying its buffer. See
[stream-id retention](usage.md#retention-and-replay) before using `releaseStream`
to repeat a mutation.

Completed named streams without subscribers are retained up to 64 entries or
64 MiB of committed body bytes and cached request keys, whichever limit is hit
first. After eviction, `GET` and `HEAD` may start again on remount. Unsafe
methods are protected by a request tombstone for the lifetime of the JavaScript
runtime, so they are never silently replayed; `releaseStream(id)` clears that
history when an explicit repeat is intended.

## Refs

```ts
function useStreamTextRef(): StreamTextRef

interface StreamTextHandle {
  attach(source: ArrayBuffer | TextStream | undefined): void
  clear(): void
  getStatus(): StreamTextState
  getText(): Promise<string>
}
```

`StreamTextRef` combines a React ref with these methods. It is safe to call
`attach()` before its view mounts; the latest pending attachment is applied on
mount.

```ts
interface StreamTextState {
  status: StreamTextStatus
  bytes: number
}
```

## Type exports

`StreamTextProps`, `StreamTextRequest`, `StreamTextHandle`, `StreamTextRef`,
`StreamTextStatus`, `StreamTextState`, `StreamTextAlign`,
`StreamTextFontWeight`, `StreamFetchInit`, `StreamFetchResponse`,
`StreamFetchResult`, and `TextStream` are exported for TypeScript users.
