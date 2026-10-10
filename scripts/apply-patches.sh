#!/bin/sh
# Applies patches/<package>@<version>.patch to node_modules/<package>; already-applied patches are skipped.
cd "$(dirname "$0")/.." || exit 1
for p in patches/*@*.patch; do
  pkg=$(basename "$p" | sed 's/@[^@]*\.patch$//')
  [ -d "node_modules/$pkg" ] || continue
  if patch -p1 -N -s --dry-run -d "node_modules/$pkg" < "$p" >/dev/null 2>&1; then
    patch -p1 -N -s -d "node_modules/$pkg" < "$p" && echo "patched $pkg"
  fi
done
