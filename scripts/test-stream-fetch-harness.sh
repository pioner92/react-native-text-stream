#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
platform=${1:?Pass ios or android}
case "$platform" in ios|android) ;; *) exit 2 ;; esac
# Own both fixture processes; don't reuse an unknown listener or leave a server behind in CI.
node server/stream-server.mjs > "${TMPDIR:-/tmp}/stream-fetch-server.log" 2>&1 &
stream_pid=$!
node server/fetch-test-server.mjs > "${TMPDIR:-/tmp}/stream-fetch-fixtures.log" 2>&1 &
fixture_pid=$!
trap 'kill "$stream_pid" "$fixture_pid" 2>/dev/null || true' EXIT
for port in 9877 9878; do
  for attempt in {1..50}; do
    kill -0 "$stream_pid" "$fixture_pid"
    if curl --silent --fail --max-time 1 "http://127.0.0.1:$port/empty?repeat=0" > /dev/null; then break; fi
    if [ "$attempt" = 50 ]; then echo "Fixture $port did not start" >&2; exit 1; fi
    sleep 0.1
  done
done
if [ "$platform" = android ]; then
  adb reverse tcp:9877 tcp:9877
  adb reverse tcp:9878 tcp:9878
fi
cd example
bun x --no-install react-native-harness --config jest.harness.config.mjs --harnessRunner "$platform" \
  --watchman=false --testPathPatterns='(fetch|streamid).*harness'
