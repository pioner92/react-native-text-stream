# Native Markdown streaming

A **formatted answer** with *emphasis*, ~~an old idea~~ and `inline code`. Привет 🌍 — العربية — 日本語.

## A small plan

1. Receive UTF-8 bytes in native memory.
2. Parse Markdown away from the UI thread.
   - Keep **completed content** stable when possible.
   - Update the changed text and its styles.
3. Show the answer while JavaScript is busy.

> A quote can contain **bold text** and a [link](https://commonmark.org).

- [x] Native transport
- [x] Markdown formatting
- [ ] Measure on the target device

```typescript
const answer = streamFetch(endpoint, { streamId: 'answer' });
// Tokens keep arriving without a React render per chunk.
```

| Stage | Work |
| --- | --- |
| Network | Append bytes |
| Parser | Build styled text |
| UI | Apply the changed range |

Entities: &amp; &lt; &gt; &#x1F680;. An escaped \*asterisk\* stays literal.

A [reference link][docs] is resolved when its definition arrives.

[docs]: https://spec.commonmark.org
