# StreamText

**Native text streaming for React Native.** Render plain-text AI answers, logs, and transcripts
without sending each chunk through JavaScript.

```tsx
import { StreamText } from 'react-native-text-stream';

export function Answer({ url }: { url: string }) {
  return <StreamText request={{ url }} fontSize={16} lineHeight={24} />;
}
```

The request writes to a native buffer using URLSession on iOS or React Native's OkHttp client on
Android. The native view consumes the text and updates its layout. No additional fetch library,
Nitro Modules, or Reanimated dependency is required.

## Status and installation

**Experimental preview.** Install the package from npm:

```sh
npm install react-native-text-stream
```

Install iOS pods and rebuild the native app after adding the package. Follow the
[installation guide](https://github.com/pioner92/react-native-text-stream/blob/master/docs/getting-started.md)
for platform steps and local-development installation. The repository root is a workspace wrapper,
not the installable library.

## Compatibility

- Developed and tested with **React Native 0.86.0**, with the **New Architecture**.
- The example targets **iOS 15.1+**; the podspec follows React Native's minimum deployment target.
- **Android API 24+**; viewport drawing optimizations require API 29+.
- Custom native build required. Expo Go, the old architecture, and web are unsupported;
  Expo integration has not been validated.

StreamText currently renders raw UTF-8 text. Native SSE/NDJSON parsing and Markdown rendering
are planned, not available features.

## Documentation

- [Overview and performance](https://github.com/pioner92/react-native-text-stream#readme)
- [Usage and lifecycle guides](https://github.com/pioner92/react-native-text-stream/blob/master/docs/usage.md)
- [API reference](https://github.com/pioner92/react-native-text-stream/blob/master/docs/api.md)
- [Example app](https://github.com/pioner92/react-native-text-stream/blob/master/example/README.md)
- [Changelog](https://github.com/pioner92/react-native-text-stream/blob/master/CHANGELOG.md)
- [Report an issue](https://github.com/pioner92/react-native-text-stream/issues)

## License

[MIT](LICENSE) © Alex Shumihin
