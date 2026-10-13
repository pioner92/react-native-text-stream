#!/bin/sh
# Permanent macOS CI regression: compile the production DrawToken under TSan, not a copied token.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_file="$repo_root/packages/react-native-text-stream/ios/fabric/StreamTextPreparedLayout.swift"
tmp_dir=$(mktemp -d /private/tmp/draw-token-tsan.XXXXXX)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

fixture="$tmp_dir/fixed.swift"
binary="$tmp_dir/fixed"
output="$tmp_dir/output.log"

extract_token() {
  sed -n '/^private final class DrawToken: @unchecked Sendable {/,/^}/p' "$source_file"
}

printf '%s\n' 'import Foundation' '' 'private final class StreamTextCanvas {}' '' > "$fixture"
extract_token >> "$fixture"
if ! grep -q '^[[:space:]]*private let lock = NSLock()$' "$fixture"; then
  printf '%s\n' 'Could not extract the synchronized production DrawToken.' >&2
  exit 1
fi
printf '%s\n' '' \
  'private let token = DrawToken(nil)' \
  'private let workers = DispatchGroup()' \
  'private let ready = DispatchGroup()' \
  'private let start = DispatchSemaphore(value: 0)' \
  'private let writerStarted = DispatchSemaphore(value: 0)' \
  'private let queue = DispatchQueue(label: "StreamText.DrawToken.TSan", attributes: .concurrent)' \
  '' \
  'for _ in 0..<3 {' \
  '  workers.enter()' \
  '  ready.enter()' \
  '  queue.async {' \
  '    ready.leave()' \
  '    start.wait()' \
  '    var observed = 0' \
  '    for _ in 0..<200_000 { if token.cancelled { observed += 1 } }' \
  '    precondition(observed >= 0)' \
  '    workers.leave()' \
  '  }' \
  '}' \
  'ready.enter()' \
  'workers.enter()' \
  'queue.async {' \
  '  ready.leave()' \
  '  ready.wait()' \
  '  for _ in 0..<3 { start.signal() }' \
  '  token.cancel()' \
  '  writerStarted.signal()' \
  '  Thread.sleep(forTimeInterval: 0.01)' \
  '  for _ in 0..<199_999 { token.cancel() }' \
  '  workers.leave()' \
  '}' \
  'writerStarted.wait()' \
  'var mainObserved = 0' \
  'for _ in 0..<200_000 { if token.cancelled { mainObserved += 1 } }' \
  'precondition(mainObserved >= 0)' \
  'workers.wait()' \
  'precondition(token.cancelled)' \
  'print("DrawToken TSan stress passed")' >> "$fixture"

swiftc -sanitize=thread "$fixture" -o "$binary"
if ! "$binary" > "$output" 2>&1; then
  sed -n '1,200p' "$output" >&2
  exit 1
fi
if grep -q 'ThreadSanitizer: data race' "$output"; then
  sed -n '1,200p' "$output" >&2
  exit 1
fi
grep -q '^DrawToken TSan stress passed$' "$output"
