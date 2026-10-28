#!/usr/bin/env bash
# Applies the project's SDK patches (the same list and order as
# scripts/build-sdk.ps1) and then the Android patch to the sdk submodule.
# Patches that are already applied are detected and left as they are; a patch
# that neither applies nor is found applied stops with an error.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk="$root/sdk"

patches=(
  rexglue-enable-usbcam.patch
  rexglue-discovery.patch
  rexglue-xma-decoder.patch
  rexglue-mnk-modifiers.patch
  rexglue-pc-features.patch
  rexglue-direct-mouse.patch
  rexglue-android.patch
)

if [[ "$(head -n1 "$sdk/thirdparty/libmspack/cabextract/mspack/lzxd.c")" == "../../libmspack/mspack/lzxd.c" ]]; then
  echo "The libmspack checkout contains symlink placeholders: initialize the SDK submodules with core.symlinks=true." >&2
  exit 1
fi

for name in "${patches[@]}"; do
  patch="$root/patches/$name"
  if git -C "$sdk" apply --check "$patch" 2>/dev/null; then
    git -C "$sdk" apply "$patch"
    echo "Applied $name"
  elif git -C "$sdk" apply --reverse --check "$patch" 2>/dev/null; then
    echo "Already applied: $name"
  else
    echo "SDK patch $name cannot be applied or verified. Existing SDK changes were preserved." >&2
    exit 1
  fi
done
