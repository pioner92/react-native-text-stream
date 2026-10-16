# Changelog

All notable changes to StreamText are documented in this file.

## 0.2.0 — 2026-10-16

### Added

- Native CommonMark/GFM rendering through `format="markdown"` and `markdownFlavor`.
- Native table grids with wrapped/aligned cells, asynchronous HTTP(S) images, task markers,
  and lexical highlighting for JavaScript, TypeScript, JSON and Python fences.
- Progressive table headers/cells, pending image placeholders and incomplete inline previews.
  Terminal statuses always restore an exact parse of the original source.
- Native SSE/NDJSON framing with OpenAI Chat/Responses, Anthropic and Ollama presets;
  custom JSON Pointer extraction, bounded event size and parser-aware request identity.
- Native parser/renderer seam tests, HTTP fixture tests in CI, and a complete streaming demo.

### Fixed

- Literal code/HTML text corruption from synthetic or shortened inline projections,
  rejected-projection cache transitions, Markdown dialect/fence handling and task marker ownership.
- iOS parser restarts on width/color changes, excessive long-paragraph patch rewinds,
  lost code/task/table styling, accessibility spacing and image request continuity on resize.
- Android paragraph line-height behavior, suffix span cleanup, Markdown worker finality,
  plain-text preparation and code/table/image geometry.
- JNI allocation failure handling and native Markdown projection size validation.

### Scope

- Markdown parsing still consumes full snapshots; progressive presentation and native suffix
  updates do not constitute a fully incremental parser or constant-time layout.
- Image requests use a separate client without stream credentials/cookies; incomplete URLs
  do not start downloads. Existing network/decode limits and unsupported schemes remain enforced.
- Experimental, New Architecture only; developed and tested with React Native 0.86.0.

## 0.1.0 — 2026-10-13

First experimental release of native plain-text streaming for React Native.

### Added

- `StreamText` native view for rendering an HTTP response as UTF-8 text arrives.
- `streamFetch()` for native streaming requests on iOS and Android.
- `streamId` support for streams that outlive a mounted view, including cancellation and release APIs.
- Request-prop, stream-source, and imperative-ref integration paths.
- Styling, native text selection, lifecycle status callbacks, request capacity, timeout, and abort-signal options.
- Documentation, an example app, and reproducible benchmark data.

### Current scope

- React Native New Architecture only; developed and tested with React Native 0.86.0.
- iOS and Android support. Expo Go, web, and the old architecture are unsupported.
- Plain UTF-8 response bodies only. SSE/NDJSON parsing and Markdown rendering are not included.
