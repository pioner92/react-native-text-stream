#!/bin/sh
# Review diagnostics: four intentionally failing assertions on the reviewed implementation.
set -eu
cd "$(dirname "$0")/../.."
target=packages/react-native-text-stream/src/__tests__/review-probes.test.tsx
if [ -e "$target" ]; then
  echo "Refusing to overwrite $target" >&2
  exit 2
fi
cp reviews/2026-10-13/react-probes.tsx "$target"
trap 'rm -f "$target"' EXIT HUP INT TERM
bun run test --runInBand --watchman=false --runTestsByPath src/__tests__/review-probes.test.tsx
