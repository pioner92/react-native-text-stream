#!/bin/sh
# Rewrites patches/react-native-nitro-fetch+native-buffer.patch: the vendored nitro-fetch's changes
# since it was set up as a workspace (cf3ef8d), new untracked files included.
cd "$(dirname "$0")/.." || exit 1
pkg=packages/react-native-nitro-fetch
out=patches/react-native-nitro-fetch+native-buffer.patch
{
  git diff cf3ef8d -- "$pkg"
  git ls-files --others --exclude-standard -- "$pkg" | while read -r f; do
    git diff --no-index -- /dev/null "$f"
  done
} > "$out"
echo "wrote $out ($(grep -c '^diff --git' "$out") files)"
