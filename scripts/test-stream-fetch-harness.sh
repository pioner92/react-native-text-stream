#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
platform=${1:?Pass ios or android}
suite=${2:-fetch}
case "$platform" in ios|android) ;; *) exit 2 ;; esac
case "$suite" in fetch|renderer|review|teardown|all) ;; *) exit 2 ;; esac
results=${HARNESS_RESULTS_DIR:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/streamtext-harness-$platform}
mkdir -p "$results"
results=$(cd "$results" && pwd)
# Own the fixtures: a conflicting listener must fail, never contaminate another run's evidence.
node server/stream-server.mjs > "$results/stream-server.log" 2>&1 &
stream_pid=$!
node server/fetch-test-server.mjs > "$results/fetch-fixtures.log" 2>&1 &
fixture_pid=$!
REVIEW_EVIDENCE_PATH="$results/evidence.jsonl" node server/review-test-server.mjs > "$results/review-fixtures.log" 2>&1 &
review_pid=$!
trap 'kill "$stream_pid" "$fixture_pid" "$review_pid" 2>/dev/null || true' EXIT
for port in 9877 9878 19878; do
  for attempt in {1..50}; do
    kill -0 "$stream_pid" "$fixture_pid" "$review_pid"
    route='/empty?repeat=0'
    if [ "$port" = 19878 ]; then route='/count?key=ready'; fi
    if curl --silent --fail --max-time 1 "http://127.0.0.1:$port$route" > /dev/null; then break; fi
    if [ "$attempt" = 50 ]; then echo "Fixture $port did not start" >&2; exit 1; fi
    sleep 0.1
  done
done
if [ "$platform" = android ]; then
  for port in 9877 9878 19878; do adb reverse "tcp:$port" "tcp:$port"; done
fi
cd example
suites=("$suite")
if [ "$suite" = all ]; then
  suites=(fetch renderer review)
  # This test deliberately stops its Fabric surface, so it must run in a separate app session last.
  if [ "$platform" = android ]; then suites+=(teardown); fi
fi
failed=0
for group in "${suites[@]}"; do
  test_timeout=${HARNESS_TEST_TIMEOUT:-30000}
  proofs=0
  case "$group" in
    fetch) pattern='(fetch|streamid|seams|js-stream).*harness' ;;
    renderer) pattern='(engines|flow|lifecycle|prepared|ref|resize|smoke|status|text|tiles|utf8)\.harness' ;;
    review) pattern='review-repro\.harness'; test_timeout=${HARNESS_REVIEW_TIMEOUT:-240000}; proofs=1 ;;
    teardown) pattern='registry-retention\.harness'; proofs=1 ;;
  esac
  # Jest JSON preserves every actually executed case and platform skip, including on failure.
  HARNESS_TEST_TIMEOUT=$test_timeout HARNESS_REVIEW_PROOFS=$proofs \
    bun x --no-install react-native-harness --config jest.harness.config.mjs --harnessRunner "$platform" \
    --watchman=false --testPathPatterns="$pattern" --json --outputFile="$results/$group.json" \
    > "$results/$group.log" 2>&1 || failed=1
  cat "$results/$group.log"
done
exit "$failed"
