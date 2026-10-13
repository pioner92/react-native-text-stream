# TSB1: native stream buffer

What `streamFetch(url, init)` returns as `stream.buffer`: the response body as an append-only log in
native memory. One writer (the library's native fetcher), any number of readers.

`StreamText` also accepts a TSB1 buffer from another native producer. The producer must own the buffer
for as long as any view or reader uses it.

## Layout

64-byte header, then the data. Little-endian.

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 4 | `magic` | `0x31425354` ("TSB1") |
| 4 | 2 | `version` | `1` |
| 6 | 2 | `headerSize` | `64` |
| 8 | 8 | `committed` | bytes written so far (atomic) |
| 16 | 8 | `capacity` | data bytes after the header |
| 24 | 4 | `status` | `0` open, `1` done, `2` error, `3` cancelled, `4` overflow (atomic) |
| 28 | 36 | reserved | `0` |
| 64 | … | data | body bytes; `[0, committed)` never changes |

## Writer (`streamFetch` or another native producer)

1. Copy the chunk to `data + committed`, then store `committed += n` (release).
2. If it doesn't fit, store `status = 4` (release) and stop.
3. At the end, store `status` (release), exactly once, after the last `committed`.

## Reader

Load `status`, then `committed` (acquire); read `[offset, committed)`. `status != 0` means no more bytes.

## Memory

`mmap`-backed (pages committed on write), never moved or resized, freed with the last
`ArrayBuffer` that wraps it.

## Write points

| | Append | Status |
|---|---|---|
| iOS | `URLSession:dataTask:didReceiveData:` | `URLSession:task:didCompleteWithError:` |
| Android | OkHttp response-body read | response completion, error or cancellation |

Reference: [`TextStreamBuffer.h`](../packages/react-native-text-stream/ios/TextStreamBuffer.h)
(`tsb_init`, `tsb_append`, `tsb_set_status`).
