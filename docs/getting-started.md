# Getting started

[← README](../README.md) · [Usage](usage.md) · [API reference](api.md)

StreamText is an experimental native component for rendering **plain UTF-8 streaming text**.

## Requirements

| Area | Requirement |
| --- | --- |
| React Native | The repository uses **0.86.0** with the New Architecture. Other versions have not been validated. |
| iOS | The example targets **15.1+**. The podspec follows React Native's `min_ios_version_supported`. Use the Xcode and CocoaPods versions required by your React Native app. |
| Android | **API 24+**, with JDK 17. The example uses compile/target SDK 36. Viewport drawing optimizations require API 29+. |
| Repository tooling | **Bun 1.4.2** and **Node.js 22.11+**, plus your platform's React Native native-build setup. |

The library depends on React and React Native. It does not require Nitro Modules, Reanimated,
Worklets, or a separate fetch library. Those packages appear in the example for comparisons and
native test helpers.

Expo Go cannot load this custom native component. An Expo app would need a custom native build;
Expo integration has not been validated. Web and the old React Native architecture are unsupported.

## Try the example

```sh
git clone https://github.com/pioner92/react-native-text-stream.git
cd react-native-text-stream
bun install
```

`bun install` also applies the repository's example-tooling patches through `postinstall`.
Follow the [example app instructions](../example/README.md) to start the streaming server,
Metro, and an iOS or Android build.

## Install from npm

From your React Native application directory:

```sh
npm install react-native-text-stream
```

For iOS, install the native pods using your app's usual CocoaPods setup, for example:

```sh
cd ios
bundle exec pod install
```

Rebuild the native app on either platform. Reloading JavaScript alone cannot register a new native
component. Autolinking is supplied by the package; there is no StreamText-specific Babel plugin.

## Local development installation

Build a package from the library workspace. The repository root is a private workspace wrapper;
installing the GitHub repository root is not a supported package installation.

From the cloned repository:

```sh
bun install
bun run build
cd packages/react-native-text-stream
npm pack
```

This creates `react-native-text-stream-0.1.0.tgz` for the current package version. Use the filename
printed by `npm pack` if the version changes. The build generates the JavaScript output and
TypeScript declarations that are not stored in Git.

From your application directory, install that archive:

```sh
npm install /absolute/path/to/react-native-text-stream/packages/react-native-text-stream/react-native-text-stream-0.1.0.tgz
```

For iOS, install the native pods using your app's usual CocoaPods setup, for example:

```sh
cd ios
bundle exec pod install
```

Rebuild the native app on either platform. Reloading JavaScript alone cannot register a new native
component. Autolinking is supplied by the package; there is no StreamText-specific Babel plugin.

## Render your first stream

Provide an endpoint that sends text incrementally:

```tsx
import { StreamText } from 'react-native-text-stream';

export function Answer({ endpoint }: { endpoint: string }) {
  return (
    <StreamText
      request={{ url: endpoint }}
      fontSize={16}
      lineHeight={24}
      style={{ padding: 16 }}
    />
  );
}
```

The component fills the width provided by its parent and grows to fit its text. Put it in a
`ScrollView` or list when the content can exceed the screen. For POST bodies, cancellation,
completion, and persistent list items, see [usage and recipes](usage.md).

## Backend response format

Return the text you want displayed, encoded as UTF-8. StreamText renders the response body as-is:

```text
The first part of the answer…
```

It does not extract text from SSE events or JSON objects. A body such as
`data: {"delta":"Hello"}` will appear literally. If your model provider returns SSE or NDJSON,
have your backend extract the text before returning it to this component. Markdown syntax is
also displayed as plain text.

For a plain-text endpoint, send:

```http
Content-Type: text/plain; charset=utf-8
X-Content-Type-Options: nosniff
```

The `nosniff` header avoids URLSession's initial content-sniffing buffer for `text/plain` on iOS.
Your server and any proxy also need to flush chunks instead of buffering the whole response.
Streaming through a proxy should be checked against your deployment configuration.

HTTP error responses still have renderable bodies. Use `streamFetch().response` and check `ok`
when your UI needs separate error handling; [the API reference](api.md) explains transport and
view completion separately.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Native component or module cannot be found | Enable the New Architecture, install iOS pods, and rebuild the native app after adding the package. |
| Text appears only at the end | Check server/proxy buffering and the iOS `nosniff` header. |
| SSE or JSON syntax appears in the view | Convert the response to plain text on your backend. Native SSE/NDJSON parsing is planned. |
| Local server cannot be reached on Android | Reverse port 9877 as described in the [example guide](../example/README.md). |
| A changed request restarts unexpectedly | Request values are compared; an `AbortSignal` is compared by identity. Keep the controller stable for that request's lifetime. |
| A retained POST does not restart after cache eviction | This is intentional no-replay behavior. See [stream lifecycles](usage.md) before releasing or reusing an id. |

## Next steps

- [Choose a request, stream id, or ref](usage.md).
- [Inspect all props and fetch options](api.md).
- [Run the demo and benchmark screens](../example/README.md).
- [Read the benchmark methodology](benchmarks.md).
