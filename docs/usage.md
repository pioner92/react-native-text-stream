# Usage

[← README](../README.md) · [API reference](api.md) · [Architecture](architecture.md)

`StreamText` renders a native, append-only text stream. Choose one ownership
model for each view: a request prop, a shared `streamId`, or a stream attached
through a ref.

## 1. Let the view make the request

Use `request` when the view owns the request. It starts on mount, restarts when
the normalized request changes, and is cancelled on unmount.

```tsx
import { StreamText } from 'react-native-text-stream'

export function Answer({ prompt, token }: { prompt: string; token: string }) {
  return (
    <StreamText
      request={{
        url: 'https://api.example.com/answer',
        method: 'POST',
        headers: {
          Authorization: `Bearer ${token}`,
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ prompt }),
      }}
      fontSize={16}
      lineHeight={22}
      color="#111827"
      style={{ padding: 12, borderRadius: 16, backgroundColor: '#f1f1f1' }}
    />
  )
}
```

`request` is compared by its outgoing value, so an inline object is valid. An
`AbortSignal` is different: its identity is part of the request. Create a signal
once with `useMemo`, `useRef`, or state instead of creating it during render.

## 2. Share a request with a list by `streamId`

Give the request and its view the same application-owned id when a chat cell can
unmount and mount again. The request and view may appear in either order.

```tsx
import { StreamText, streamFetch } from 'react-native-text-stream'

type Message = { id: string; prompt: string }

function answerStreamId(messageId: string) {
  return `assistant:${messageId}`
}

// A store, event handler, or service can start the request.
export function startAnswer(message: Message) {
  return streamFetch('https://api.example.com/answer', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ prompt: message.prompt }),
    streamId: answerStreamId(message.id),
  })
}

// The list cell can mount later, unmount, and mount again.
export function AssistantMessage({ messageId }: { messageId: string }) {
  return <StreamText streamId={answerStreamId(messageId)} fontSize={16} lineHeight={22} />
}
```

`<StreamText streamId={id} request={request} />` is also valid. With a supplied
id, the request belongs to the id rather than the view, so unmounting the view
does not cancel it.

### Retention and replay

Streams with no subscriber are retained by the native registry for reuse, up to
its cache limits. A remount can reuse a retained stream. If a completed `GET` or
`HEAD` entry was evicted, mounting the same `request` can send it again.

`POST` and other unsafe methods are deliberately not silently replayed after
eviction. They remain empty until the request changes or you explicitly release
the id. Use an idempotency key at the application/server boundary when a request
may legitimately be repeated.

```tsx
import { abortStream, releaseStream } from 'react-native-text-stream'

export function discardAnswer(id: string) {
  abortStream(id) // Stops the named request; its committed prefix remains.
  releaseStream(id) // Forgets registry retention; mounted views keep their buffer.
}
```

Call `releaseStream(id)` when the message is no longer needed or before an
intentional repeat. A later request under the same id replaces—and cancels—the
previous named request.

## 3. Start imperatively and attach a stream

Use this when an action begins the request and the view should update without
putting the `TextStream` in React state.

```tsx
import { useEffect, useRef } from 'react'
import { Pressable, Text } from 'react-native'
import {
  StreamText,
  streamFetch,
  useStreamTextRef,
  type TextStream,
} from 'react-native-text-stream'

export function Composer({ prompt }: { prompt: string }) {
  const streamRef = useStreamTextRef()
  const active = useRef<TextStream | null>(null)

  useEffect(() => () => active.current?.abort(), [])

  function ask() {
    active.current?.abort()
    const stream = streamFetch('https://api.example.com/answer', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt }),
    })
    active.current = stream
    streamRef.attach(stream)
  }

  return (
    <>
      <Pressable onPress={ask}><Text>Ask</Text></Pressable>
      <StreamText ref={streamRef} fontSize={16} lineHeight={22} />
    </>
  )
}
```

`useStreamTextRef()` is stable and accepts an attachment before the view mounts.
`clear()` detaches the current stream. `getStatus()` returns the latest known
`{ status, bytes }`; `getText()` asynchronously decodes the committed text.

Using `source` is the declarative equivalent when the stream belongs in state:

```tsx
import { StreamText, type TextStream } from 'react-native-text-stream'

export function Answer({ stream }: { stream: TextStream | null }) {
  return <StreamText source={stream} fontSize={16} />
}
```

## Completion, failures, and HTTP status

`TextStream.response` resolves as soon as HTTP headers arrive. A non-2xx response
still has a body and streams it; check `response.ok` yourself. It rejects when
the request fails before a response exists.

`TextStream.done` always resolves with `done`, `error`, `cancelled`, or
`truncated`. The `truncated` status means the configured byte capacity was
reached and the rest of the response was dropped.

```tsx
import { streamFetch } from 'react-native-text-stream'

export async function inspectAnswer() {
  const stream = streamFetch('https://api.example.com/answer', { timeout: 60_000 })

  try {
    const response = await stream.response
    if (!response.ok) console.warn(`HTTP ${response.status}`)
  } catch (error) {
    console.warn('No HTTP response', error)
  }

  const { status, error } = await stream.done
  if (status === 'error') console.warn(error)
  return stream
}
```

`onStatusChange` serves a different purpose: it reports `streaming`, then a
terminal view status after the buffered text has been displayed. Use it for UI
state such as hiding a typing indicator; use `response` and `done` for transport
state.

```tsx
import { StreamText, type StreamTextStatus } from 'react-native-text-stream'

const terminal = new Set<StreamTextStatus>(['done', 'error', 'cancelled', 'truncated'])

export function AnswerStatus() {
  return (
    <StreamText
      request={{ url: 'https://api.example.com/answer' }}
      onStatusChange={(status, bytes) => {
        if (terminal.has(status)) console.log(`Displayed ${bytes} bytes: ${status}`)
      }}
    />
  )
}
```

## Request options

| Option | Default | Notes |
| --- | --- | --- |
| `method` | `GET`, or `POST` with `body` | `GET` and `HEAD` cannot have a body. |
| `headers` | — | A record, tuple list, or `Headers`-like object. Names are normalized to lower case. |
| `body` | — | A UTF-8 string. |
| `signal` | — | Aborts the request. A pre-aborted signal starts no network request. |
| `capacity` | 16 MiB | Maximum decoded body bytes to retain; overflow ends as `truncated`. |
| `timeout` | 5 min | Deadline through response headers, then an idle-read timeout; it is not a total transfer limit. |
| `streamId` | — | Native registry key shared with `<StreamText streamId>`. |
| `parser` | — | Native SSE/NDJSON framing and text extraction; see [protocols](protocols.md). |

## Protocols and text formats

The component renders UTF-8 text. Select `format="markdown"` for native
[CommonMark/GFM rendering](markdown.md). For SSE or NDJSON endpoints, configure
a request `parser` to extract text before it reaches the buffer; see
[provider presets and custom fields](protocols.md). Without a parser, the raw wire
body appears in the view.

For a native producer, pass its native-owned TSB1 `ArrayBuffer` to `source` or
`attach()`. JavaScript-created `ArrayBuffer`s are not supported. See
[the architecture guide](architecture.md#native-producers-and-tsb1).

## Practical notes

- `StreamText` lays out at the width it receives and
  grows with the text; use normal React Native view styles for its container.
- A finished message still needs native layout work when mounted. Measure your
  own message sizes and screen hardware; see [benchmarks](benchmarks.md).
- The library keeps the network-to-view update path out of JavaScript. It does
  not make all UI work free, and results depend on payload, fonts, layout, and
  device.
