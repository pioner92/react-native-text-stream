# Contributing to StreamText

Thank you for improving StreamText. Please open an issue before beginning a
large API, rendering, or networking change so the intended behaviour can be
reviewed before implementation.

## Development setup

Use Bun 1.4.2 and Node 22.11 or newer. The example also needs the normal
React Native toolchains: Xcode and CocoaPods for iOS, or Android Studio, an SDK,
and an emulator/device for Android.

```sh
git clone https://github.com/pioner92/react-native-text-stream.git
cd react-native-text-stream
bun install
```

Run the example with the instructions in [example/README.md](example/README.md).
It is a workspace consumer of the package, so changes under
`packages/react-native-text-stream` are immediately exercised by the app.

## Before opening a pull request

Run the checks relevant to the change. These commands are intended to be run
from the repository root.

### TypeScript and package checks

```sh
bun run build
bun run test
bun run --cwd example test
bun x --no-install tsc --noEmit --project example/tsconfig.json --ignoreDeprecations 6.0
```

`bun run build` typechecks the library and builds the distributable `lib/`
files. Do not commit those generated files.

### Native invariants

```sh
bun run test:native
```

This runs the C++ stream-fetch invariants with AddressSanitizer and
UndefinedBehaviorSanitizer. On macOS it also runs the iOS delegate coverage.
For changes to `DrawToken` or its threading contract, additionally run:

```sh
sh scripts/test-draw-token-tsan.sh
```

### Android JVM and device checks

```sh
(cd example/android && ./gradlew :react-native-text-stream:testDebugUnitTest --no-daemon --build-cache)
```

With an Android emulator or device available, run the connected native tests:

```sh
(cd example/android && ./gradlew :react-native-text-stream:connectedDebugAndroidTest --no-daemon)
```

### In-app renderer and fetch harnesses

Build or install the example app for the chosen platform, then run:

```sh
bash scripts/test-stream-fetch-harness.sh ios all
bash scripts/test-stream-fetch-harness.sh android all
```

The script starts its own fixture servers and, for Android, configures `adb
reverse` for the fixture ports. Set `HARNESS_APP_PATH` when the harness should
launch an app bundle or APK that is not already installed. To run only one
group, replace `all` with `fetch`, `renderer`, `review`, or `teardown` (the
last group is Android-only).

## Change guidelines

- Preserve the no-JavaScript-per-chunk rendering path.
- Add a focused regression test for lifecycle, cancellation, UTF-8, or
  scheduling bugs. Put platform-specific coverage beside the owning native
  code when practical.
- Keep the public API and documentation in sync. Explain behavioural changes
  in the pull request, including which platforms were exercised.
- Do not update benchmark figures from debug builds or runs touched by UI
  automation. Follow the [benchmark guide](docs/benchmarks.md).

## Pull requests

Keep pull requests small enough to review. Describe the user-visible change,
the relevant platform coverage, and any limitation that remains. Do not include
generated build products, Pods, Gradle output, or benchmark result files unless
they are the requested artifact.
