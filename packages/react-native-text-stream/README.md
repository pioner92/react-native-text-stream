# react-native-text-stream

A native React Native text view that renders a streamed response as it arrives, without JS per chunk.

```tsx
const res = await fetch(url, { stream: true, nativeBuffer: true });

<StreamText source={res.nativeBuffer} />
```

## Requirements

- React Native 0.86 or higher, with the New Architecture
- iOS 15.1 or higher
- Android 7.0 (API 24) or higher

## Status

Experimental, not yet on npm. The `nativeBuffer` option comes from a patched `react-native-nitro-fetch`
vendored in the repository.

Usage, API, how it works and benchmarks: see the
[repository README](https://github.com/alexshumihin/react-native-text-stream#readme).

## License

MIT
