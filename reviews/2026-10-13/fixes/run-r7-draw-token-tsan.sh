#!/bin/sh
# Reproducible TSan proof for R7.  The fixed fixture extracts DrawToken directly from production
# source; only StreamTextCanvas is stubbed because UIKit is irrelevant to the shared Bool.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$repo_root/packages/react-native-text-stream/ios/fabric/StreamTextPreparedLayout.swift"
output_dir="$repo_root/reviews/2026-10-13/fixes"
tmp_dir=$(mktemp -d /private/tmp/r7-draw-token-tsan.XXXXXX)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

fixed_source="$tmp_dir/fixed.swift"
before_source="$tmp_dir/before.swift"
extracted_token="$output_dir/r7-draw-token-production-token.swift"

extract_token() {
  sed -n '/^private final class DrawToken: @unchecked Sendable {/,/^}/p' "$source_file"
}

write_fixture() {
  target=$1
  printf '%s\n' 'import Foundation' '' 'private final class StreamTextCanvas {}' '' > "$target"
  extract_token >> "$target"
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
    'print("DrawToken TSan stress passed")' >> "$target"
}

write_fixture "$fixed_source"
extract_token > "$extracted_token"
sed \
  -e '/private let lock = NSLock()/d' \
  -e 's/private var isCancelled = false/var cancelled = false/' \
  -e '/var cancelled: Bool { lock.withLock { isCancelled } }/d' \
  -e 's/func cancel() { lock.withLock { isCancelled = true } }/func cancel() { cancelled = true }/' \
  "$fixed_source" > "$before_source"

printf '%s\n' \
  "source=$source_file" \
  'command=rtk proxy sh reviews/2026-10-13/fixes/run-r7-draw-token-tsan.sh' \
  'fixed fixture: swiftc -sanitize=thread <production DrawToken extraction + stress>' \
  'control: same extraction with only the lock/state lines reverted; a main-thread reader overlaps an active writer before all workers are joined' \
  > "$output_dir/r7-draw-token-tsan-commands.txt"
shasum -a 256 "$extracted_token" > "$output_dir/r7-draw-token-production-token.sha256"

swiftc -sanitize=thread "$before_source" -o "$tmp_dir/before" > "$output_dir/r7-draw-token-tsan-before.log" 2>&1
before_detected=0
for attempt in 1 2 3 4 5; do
  printf '%s\n' "attempt=$attempt" >> "$output_dir/r7-draw-token-tsan-before.log"
  "$tmp_dir/before" >> "$output_dir/r7-draw-token-tsan-before.log" 2>&1 || true
  if grep -q 'ThreadSanitizer: data race' "$output_dir/r7-draw-token-tsan-before.log"; then
    before_detected=1
    break
  fi
done
if [ "$before_detected" -ne 1 ]; then
  printf '%s\n' 'Expected the historical control to emit "ThreadSanitizer: data race" in five synchronized attempts.' >&2
  exit 1
fi

swiftc -sanitize=thread "$fixed_source" -o "$tmp_dir/fixed" > "$output_dir/r7-draw-token-tsan-fixed.log" 2>&1
"$tmp_dir/fixed" >> "$output_dir/r7-draw-token-tsan-fixed.log" 2>&1
if grep -q 'ThreadSanitizer: data race' "$output_dir/r7-draw-token-tsan-fixed.log"; then
  printf '%s\n' 'ThreadSanitizer reported a race in the fixed production extraction.' >&2
  exit 1
fi
