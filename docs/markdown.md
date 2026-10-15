# Native Markdown

[← README](../README.md) · [API](api.md) · [Architecture](architecture.md)

```tsx
<StreamText
  streamId={message.id}
  format="markdown"
  markdownFlavor="gfm"
  fontSize={16}
  color="#ECECF1"
/>
```

`format` defaults to `plain`. Markdown is opt-in; it uses the same native transport,
source buffer and ownership models as plain text, without JavaScript callbacks per
chunk. `markdownFlavor` defaults to `commonmark`; `gfm` enables strikethrough, task
lists, autolinks and table parsing.

## Rendering scope

The renderer supports headings, bold/italic text, strikethrough, inline and fenced
code, ordered/unordered/nested lists, task markers, blockquotes and links. GFM tables
render as native grids with header styling, column alignment and wrapped cells.
The grid fits the available width and reflows when the view resizes.

Fenced code uses a monospace font. An explicit `js`/`javascript`/`jsx`,
`ts`/`typescript`/`tsx`, `json`, or `py`/`python` language enables native lexical
highlighting of keywords, strings, comments, numbers and TypeScript primitive types.
Unknown or missing languages remain plain monospace. This is a bounded lexical
highlighter, not a compiler: template-string interpolation is colored as a string;
JSX markup, semantic symbols and JavaScript regular-expression literals are not
separately classified.

Markdown images load asynchronously over HTTP(S). The source's terminal status does
not wait for images; image completion updates native drawing and layout without
another terminal callback. Loading and failed images retain an alt-text placeholder.
Images inside table cells use their alt text. Images fit the available width and
preserve their aspect ratio.

Link labels are styled; consistent cross-platform link activation is not part of
this API yet. Task markers are read-only. HTML rendering, math, diagrams and
horizontal table scrolling are not included. Native selection represents a table
or image as an attachment; `getText()` remains the way to retrieve the full source.

An unfinished construct is parsed according to its currently available prefix.
For example, an unmatched `**` can remain literal until the closing delimiter
arrives. This version does not invent missing Markdown delimiters. A reference
link definition may change an earlier paragraph when it arrives.

`getText()` and `TextStream.text()` return the **original source**, including
Markdown punctuation. Status event byte counts also refer to UTF-8 source bytes,
not the length of the formatted text. Native selection operates on rendered text.
SSE/NDJSON decoding is still a separate concern: feed this view extracted Markdown
text, rather than raw protocol events.

## Performance design

The package vendors a pinned MIT-licensed MD4C parser in its native core. Both
platforms share the same parser and UTF-16 style runs. Entry-point symbols are
prefixed so another library may vendor MD4C in the same app.

Streaming parsing runs on a native worker. Updates are coalesced, and results from
obsolete sources are discarded. The renderer must compare text **and styles**:
closing a delimiter or resolving a reference link can invalidate earlier text.
Plain text retains its separate append-only rendering path. A cold prepared-layout
cache miss on the main thread queues work and uses the streaming view; a later
mount can reuse the prepared canvas. Documents with table/image attachments use
the live native text view instead of the prepared canvas cache, so width changes
and asynchronous image completion cannot leave a frozen cached attachment.
Ordinary Markdown and highlighted code retain prepared-layout caching.

Table cell layout runs on the native worker; drawing uses native glyph layouts,
not a table-sized raster image. Image loading has two active requests, deduplicates
in-flight URLs, limits each response to 8 MiB while receiving bytes, downsamples
to at most 2048 pixels per edge and 4,000,000 decoded pixels, and uses a 32 MiB
shared decoded-image cache. Image requests do not inherit the stream request's
headers or cookies. Clear, source replacement and unmount detach image subscribers.
A completed text stream can therefore remain usable when an image fails.

MD4C parses a full snapshot. This version is not an incremental Markdown parser:
processing every network chunk separately would repeatedly scan the accumulated
prefix. Coalescing limits how often snapshots are processed; it does not change
that algorithmic cost. A very long single paragraph can also require substantial
native layout work. Do not apply the plain-text benchmark figures to Markdown.

## Verification

```sh
bun run test:markdown
sh scripts/test-markdown-native.sh --sanitize
sh scripts/test-markdown-native.sh --bench
```

Native parser tests compare every streamed prefix and replayed text/style patch with a
fresh parser, including reference links, nested styling, Unicode, entities and
malformed bytes. Deterministic fuzz checks retained-parser and patch invariants; it
is not an independent CommonMark conformance oracle. ASan/UBSan checks run in CI.
The `example/__tests__/markdown*.harness.tsx` suites cover native view
lifecycle, JS contention, ownership, terminal statuses, mode changes, stale worker
results and actual storage attributes through streaming, cache and resize seams.

The example's **Markdown** screen includes streaming, a 100 KB response, source
view and a JS-blocking control. It uses a separate local fixture port so it can run
alongside other demos: `PORT=9879 node server/stream-server.mjs`.
The **Rich** button uses `node server/rich-markdown-server.mjs` on port 9882. Run
that fixture server before the rich Markdown in-app tests; it supplies delayed,
invalid, missing, oversized and high-resolution images without external services.

For renderer measurements, `example/index.markdown-bench.js` is a dedicated
Release entry point. It measures native production of 5 KB/100 KB responses and
cold/warm mounting of one/twenty finished 5 KB messages. Each case runs three
times. Results include native frame pacing, source display logs, rendering phases
and process memory. The plain mode is an overhead reference using identical source
bytes; its visual layout is different. UI inspection happens after timing.

Run `node scripts/markdown-bench-collector.mjs` before launching the benchmark.
It posts JSON to the local collector at `http://localhost:19879/results`.
Numbers must identify the actual device/build and distinguish parser-only timing
from UI timing. Simulator results are not physical-device performance claims.
See [measured parser costs](markdown-performance.md).

## Initial Markdown validation on 2026-10-15

- Shared parser: native correctness suite, AddressSanitizer and UndefinedBehaviorSanitizer passed.
- JavaScript: 124 tests passed; library and example TypeScript checks passed (the example's
  pre-existing TypeScript 6 `baseUrl` deprecation requires `--ignoreDeprecations 6.0`).
- iOS: Debug and Release arm64 simulator builds passed; 25 Markdown in-app tests passed,
  plus 50 existing lifecycle/cache/status/UTF-8/ref tests (one platform-conditional skip).
- Android: Debug APK and arm64 C++/JNI builds passed; 30 JVM tests passed, including
  13 Markdown worker/diff tests. The dedicated Android 15 emulator also passed 50
  existing lifecycle/cache/status/UTF-8/ref tests and 25 Markdown in-app tests
  (one platform-conditional skip). The latter caught and verified fixes for JNI
  registration and themed-color cache reuse. Physical-device profiling was not run.
- Release renderer: 36 iOS Simulator runs; [method and raw evidence](markdown-performance.md).

The dedicated devices are `StreamText Markdown` (iPhone 17 Pro Max, iOS 26.5)
and `StreamText_Markdown_Tests` (Pixel 8, Android 15 arm64 emulator).
# Performance evidence

See [rich Markdown performance evidence](markdown-rich-performance.md).

## Rich Markdown validation on 2026-10-15

- Shared parser and fenced lexer: native correctness and ASan/UBSan passed. Prefix tests
  compare rich metadata as well as text/runs, including metadata-only updates and Unicode.
- JavaScript package: 153 tests passed; library build and example TypeScript checks passed.
- Android: 38 JVM tests passed, including width acknowledgements, metadata diffs and bounded
  accessibility expansion. Debug arm64 C++/JNI builds passed.
- Visual inspection of the final demo passed on both dedicated simulators.
- Dedicated iOS/Android rich suites cover table layout and resize, 2,056-cell native marshalling,
  syntax runs, source preservation, delayed/error/oversized/chunked image responses, pixel caps,
  in-flight deduplication, two-request concurrency, caching, source/mode/unmount cancellation,
  append/style changes during loading, accessibility and explicit line height.
- Release iOS simulator measurements and host parser comparisons are recorded in
  [rich performance evidence](markdown-rich-performance.md). They do not establish
  physical-device performance or hitch-free bulk mounting.

The latest per-test results across these runs are **126 passed / one platform skip on iOS**
and **124 passed / two failed / one platform skip on Android**. All **23 rich Markdown**
tests pass on each platform. [Validation evidence](benchmarks/markdown-rich-validation-2026-10-15.json)
identifies the selected suites and the comparison experiment.

The two Android failures are existing plain-text resize assertions: a width-cache miss can
show the prior-height canvas before switching to a newly measured text view. Both reproduced
with the baseline view/line-height implementation from `867c265`; the comparison retained
other feature files and therefore is not a clean full-baseline checkout. This feature does
not fix that plain-renderer behavior. Physical-device checks were not performed.
