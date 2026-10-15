#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sanitize=0
if [ "${1:-}" = "--sanitize" ]; then
  sanitize=1
  shift
fi
if [ "$sanitize" -eq 1 ]; then
  build_dir="${TMPDIR:-/tmp}/streamtext-markdown-native-sanitize"
  c_flags="-std=c99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
  cxx_flags="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
else
  build_dir="${TMPDIR:-/tmp}/streamtext-markdown-native"
  c_flags="-std=c99 -O3 -DNDEBUG"
  cxx_flags="-std=c++17 -O3"
fi
mkdir -p "$build_dir"
cc $c_flags -c "$root/packages/react-native-text-stream/cpp/markdown/vendor/md4c/md4c.c" -o "$build_dir/md4c.o"
cc $c_flags -c "$root/packages/react-native-text-stream/cpp/markdown/vendor/md4c/entity.c" -o "$build_dir/entity.o"
c++ $cxx_flags "$root/packages/react-native-text-stream/cpp/markdown/StreamMarkdown.cpp" "$root/tests/native/markdown_test.cpp" "$build_dir/md4c.o" "$build_dir/entity.o" -o "$build_dir/markdown_test"
"$build_dir/markdown_test" "$@"
c++ $cxx_flags -I"$root/packages/react-native-text-stream/cpp/markdown" "$root/tests/native/markdown_syntax_test.cpp" -o "$build_dir/markdown_syntax_test"
"$build_dir/markdown_syntax_test"
