#!/usr/bin/env bash
# Applies the project's SDK patches (the list and order of scripts/sdk-patches.ps1,
# which build-sdk.ps1 and build-android.ps1 use) to the sdk submodule.
# A later patch can change the lines next to an earlier one, and then the
# earlier one no longer checks in reverse: the stack is checked from the top,
# and every patch below the highest one found applied counts as applied. A
# missing patch that does not apply stops with an error.
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

applied=0
for ((i = ${#patches[@]}; i > 0; i--)); do
  if git -C "$sdk" apply --reverse --check "$root/patches/${patches[i - 1]}" 2>/dev/null; then
    applied=$i
    break
  fi
done
for ((i = 0; i < applied; i++)); do echo "Already applied: ${patches[i]}"; done
for ((i = applied; i < ${#patches[@]}; i++)); do
  patch="$root/patches/${patches[i]}"
  if ! git -C "$sdk" apply --check "$patch" 2>/dev/null; then
    echo "SDK patch ${patches[i]} cannot be applied or verified. Existing SDK changes were preserved." >&2
    exit 1
  fi
  git -C "$sdk" apply "$patch"
  echo "Applied ${patches[i]}"
done
