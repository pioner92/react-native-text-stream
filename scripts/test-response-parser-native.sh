#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sanitize=0
if [ "${1:-}" = "--sanitize" ]; then sanitize=1; shift; fi
dir=$(mktemp -d "${TMPDIR:-/tmp}/streamtext-response-parser.XXXXXX")
trap 'rm -rf "$dir"' EXIT HUP INT TERM
if [ "$sanitize" -eq 1 ]; then flags="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"; else flags="-O3"; fi
cc -std=c99 $flags -DNDEBUG -c "$root/packages/react-native-text-stream/cpp/streaming/vendor/yyjson/yyjson.c" -o "$dir/yyjson.o"
c++ -std=c++17 $flags "$root/packages/react-native-text-stream/cpp/streaming/StreamTextResponseParser.cpp" "$root/tests/native/response_parser_test.cpp" "$dir/yyjson.o" -o "$dir/test"
"$dir/test" "$@"
