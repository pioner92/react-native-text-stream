# StreamText example

This app demonstrates native text streaming beside a conventional JavaScript
implementation. It is also the home of the repeatable benchmark and native
test harnesses used by this repository.

## Run it

From the repository root, install the workspace dependencies:

```sh
bun install

# iOS only, on macOS
bun run --cwd example pod
```

Keep each of the following commands in its own terminal, all opened at the
repository root:

```sh
# Terminal 1 — local response server on port 9877
bun run --cwd example stream-server
```

```sh
# Terminal 2 — Metro on port 8081
bun run --cwd example start
```

```sh
# Terminal 3 — choose one platform
bun run --cwd example ios
```

For Android, reverse the two local ports before starting the app so the
device's `localhost` reaches the development machine:

```sh
adb reverse tcp:8081 tcp:8081
adb reverse tcp:9877 tcp:9877
bun run --cwd example android
```

The app needs a simulator, emulator, or connected device and the normal React
Native platform toolchain. The workspace uses Node 22.11 or newer.

## Explore the demo

The home screen streams the same response through two paths:

- **StreamText** sends the request and renders text natively.
- **JavaScript baseline** receives chunks with `XMLHttpRequest`, updates state,
  and re-renders `<Text>`.

Start a response, then choose **Freeze JS · 5s**. The control deliberately
blocks JavaScript so the difference between the two paths is visible. **Chat**
uses the same native stream in a growing list bubble.

## Run benchmarks

Open **Benchmarks** in a Release build, keep the local streaming server running,
and use **Run all**, **Mount**, or **History**. Result JSON is saved under
[`../bench-results/`](../bench-results/).

Timed measurements must run without UI automation, accessibility inspection,
view-tree snapshots, or screenshots: inspecting a long text view can itself
stall the UI thread and invalidate the run. The benchmark screen reports when a
run is complete; inspect the saved result afterwards.

For the scenarios, build commands, measurement definitions, and how to compare
runs, read the [benchmark guide](../docs/benchmarks.md).

## Test harnesses

The in-app harnesses exercise the native renderer and request lifecycle. From
the repository root:

```sh
bun run test:harness            # iOS simulator
bun run --cwd example test:harness:android
```

The focused lifecycle and fetch suites start their own fixture servers and
configure Android port reversal themselves:

```sh
bash scripts/test-stream-fetch-harness.sh ios all
bash scripts/test-stream-fetch-harness.sh android all
```

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the full validation matrix.

## Markdown

Open **Markdown** from the home screen to stream formatted text, switch to its
original source, test a 100 KB response, and block JS while native rendering
continues. The local server supports `?content=markdown` for these fixtures.
See [rendering scope and performance](../docs/markdown.md).

The Markdown screen uses its own fixture port: from the repository root run
`PORT=9879 node server/stream-server.mjs`.

## Protocol fixtures

`node server/protocol-server.mjs` (from the repository root) serves fragmented SSE
and NDJSON on port 9881. `protocol.harness.tsx` exercises all four provider presets,
custom JSON fields, EOF/terminal/error behavior and native progress while JS is
blocked. The fetch group in `test-stream-fetch-harness.sh` starts this server
automatically. See [protocol parsing](../docs/protocols.md).
