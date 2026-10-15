# Changelog

All notable changes to StreamText are documented in this file.

## Unreleased

- Native CommonMark/GFM rendering with `format="markdown"`, incremental updates and platform seam tests.
- Native SSE/NDJSON framing with OpenAI Chat/Responses, Anthropic and Ollama text presets.
- Custom JSON Pointer text extraction, bounded record size, terminal-marker completion and parser-aware request identity.
- Shared sanitizer coverage and iOS/Android networking-to-Markdown regression tests.

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
