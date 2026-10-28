#!/usr/bin/env bash
# Fetches the native renderer's sources an Android build needs, at the pinned
# revisions of tools/native-renderer/dependencies.json:
#   out/plume                               Plume and its Vulkan submodules
#   tools/xenosrecomp/thirdparty/smol-v     the smol-v decoder of the SPIR-V cache
# Existing checkouts at the right revision are kept.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
deps="$root/tools/native-renderer/dependencies.json"

json() { python3 -I -c "import json,sys; d=json.load(open(sys.argv[1])); print(eval('d'+sys.argv[2]))" "$deps" "$1"; }

checkout() {  # <dir> <url> <revision>
  local dir="$1" url="$2" rev="$3"
  if [[ -d "$dir/.git" ]] && [[ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" == "$rev" ]]; then
    echo "OK  $dir ($rev)"
    return
  fi
  if [[ ! -d "$dir/.git" ]]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$url"
  fi
  git -C "$dir" fetch -q --depth 1 origin "$rev"
  git -C "$dir" checkout -q --detach FETCH_HEAD
  echo "Got $dir ($rev)"
}

plume="$root/out/plume"
checkout "$plume" "$(json "['plume']['url']")" "$(json "['plume']['revision']")"
# The Vulkan submodules, at the revisions Plume pins (also listed in dependencies.json).
for sub in contrib/volk contrib/Vulkan-Headers contrib/VulkanMemoryAllocator; do
  rev="$(json "['plume']['vulkan_submodules']['$sub']")"
  url="$(git -C "$plume" config -f .gitmodules --get "submodule.$sub.url")"
  checkout "$plume/$sub" "$url" "$rev"
done

checkout "$root/tools/xenosrecomp/thirdparty/smol-v" "$(json "['xenosrecomp_thirdparty']['smol-v']['url']")" \
         "$(json "['xenosrecomp_thirdparty']['smol-v']['revision']")"

# libadrenotools (and its linkernsbypass submodule): loads the Turnip driver
# bundled with the app (tools/android/dependencies.json).
android_deps="$root/tools/android/dependencies.json"
adrenotools="$root/out/libadrenotools"
checkout "$adrenotools" \
  "$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['libadrenotools']['url'])" "$android_deps")" \
  "$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['libadrenotools']['revision'])" "$android_deps")"
git -C "$adrenotools" submodule update -q --init --recursive
