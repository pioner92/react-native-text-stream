# react-native-text-stream

A native React Native text view that renders a streamed response as it arrives, without JS per chunk.

```tsx
// The view makes the request; each piece of the body is on screen in the next frame.
<StreamText request={{ url, method: 'POST', headers, body }} />

// Or by an id the request and the view share, in any order (lists, stores):
streamFetch(url, { streamId: message.id, method: 'POST', body })
<StreamText streamId={message.id} />
```

## Requirements

- React Native 0.86 or higher, with the New Architecture
- iOS 15.1 or higher
- Android 7.0 (API 24) or higher

## Status

Experimental, not yet on npm.

Usage, API, how it works and benchmarks: see the
[repository README](https://github.com/alexshumihin/react-native-text-stream#readme).

## License

MIT
