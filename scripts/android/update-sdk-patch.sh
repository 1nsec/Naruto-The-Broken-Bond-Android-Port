#!/usr/bin/env bash
# Development helper: writes patches/rexglue-android.patch from the changes in
# the sdk working tree on top of the project's other SDK patches.
#
# The diff base is built in a temporary index: the SDK's pinned commit with the
# project patches applied (the same list as apply-sdk-patches.sh, without the
# Android patch). New files in the sdk tree are included; the SDK's own
# submodules (under thirdparty/) are ignored, its thirdparty/CMakeLists.txt is not.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk="$root/sdk"
out="$root/patches/rexglue-android.patch"
pinned="$(git -C "$root" ls-tree HEAD sdk | awk '{print $3}')"

index="$(mktemp)"
trap 'rm -f "$index"' EXIT
export GIT_INDEX_FILE="$index"

git -C "$sdk" read-tree "$pinned"
for name in rexglue-enable-usbcam rexglue-discovery rexglue-xma-decoder \
            rexglue-mnk-modifiers rexglue-pc-features rexglue-direct-mouse; do
  git -C "$sdk" apply --cached "$root/patches/$name.patch"
done
# New files: everything untracked outside the vendored submodules and build output.
git -C "$sdk" ls-files --others --exclude-standard -- . ':!thirdparty' ':!out' |
  while IFS= read -r file; do git -C "$sdk" add -N -- "$file"; done
git -C "$sdk" diff --ignore-submodules=all --no-color --no-ext-diff -- . ':!out' > "$out"
echo "Wrote $out ($(grep -c '^diff --git' "$out") files)"
