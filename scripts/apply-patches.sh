#!/bin/sh
# Applies patches/<package>@<version>.patch to node_modules/<package>.
# Already applied: skipped. Another installed version, or a patch that neither applies nor is
# already applied: reported, and the script fails once all patches were tried.
cd "$(dirname "$0")/.." || exit 1
failed=0
for p in patches/*@*.patch; do
  name=$(basename "$p" .patch)
  pkg=${name%@*}
  want=${name##*@}
  dir="node_modules/$pkg"
  [ -d "$dir" ] || continue
  have=$(node -p "require('./$dir/package.json').version" 2>/dev/null)
  if [ "$have" != "$want" ]; then
    echo "apply-patches: $p is for $pkg@$want, installed is $pkg@$have" >&2
    failed=1
  elif patch -p1 -N -s --dry-run -d "$dir" < "$p" >/dev/null 2>&1; then
    patch -p1 -N -s -d "$dir" < "$p" && echo "patched $pkg@$want"
  elif ! patch -p1 -R -s -f --dry-run -d "$dir" < "$p" >/dev/null 2>&1; then
    echo "apply-patches: $p does not apply to $dir" >&2
    failed=1
  fi
done
exit $failed
