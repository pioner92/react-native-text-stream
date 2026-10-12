#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d "${TMPDIR:-/tmp}/stream-fetch-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT
pkg=packages/react-native-text-stream
compiler=${CXX:-clang++}
"$compiler" -std=c++20 -pthread -fsanitize=address,undefined -Itests/native -I"$pkg/cpp" -I"$pkg/ios" \
  tests/native/stream_fetch_test.cpp "$pkg/cpp/StreamTextMappedBuffer.cpp" "$pkg/cpp/StreamTextStreams.cpp" -o "$build/core"
"$build/core"
if [ "$(uname -s)" = Darwin ]; then
  "$compiler" -std=c++20 -fobjc-arc -Wno-nonnull -fsanitize=address,undefined -Itests/native -I"$pkg/cpp" \
    tests/native/stream_fetch_ios_test.mm "$pkg/cpp/StreamTextMappedBuffer.cpp" "$pkg/cpp/StreamTextStreams.cpp" \
    -framework Foundation -o "$build/ios"
  "$build/ios"
fi
