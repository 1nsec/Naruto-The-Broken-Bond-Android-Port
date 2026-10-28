#!/usr/bin/env bash
# Builds Turnip, Mesa's Vulkan driver for Adreno GPUs, for Android arm64, at
# the Mesa tag of tools/android/dependencies.json. The app bundles it and
# loads it with libadrenotools when the system driver lacks what the renderer
# needs (src/android/gpu_driver.cpp).
#
#   build-turnip.sh <android ndk> <output folder>
#
# Needs git, python3 with meson and mako, ninja, bison, flex and pkg-config.
# Writes <output>/libvulkan_freedreno.so and <output>/turnip.json.
set -euo pipefail
ndk="$1"
out="$(mkdir -p "$2" && cd "$2" && pwd)"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
deps="$root/tools/android/dependencies.json"
url="$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['mesa']['url'])" "$deps")"
tag="$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['mesa']['tag'])" "$deps")"
api=29
work="${TURNIP_WORK:-$root/build/turnip}"
mkdir -p "$work"

if [[ ! -d "$work/mesa/.git" ]] || [[ "$(git -C "$work/mesa" describe --tags --exact-match 2>/dev/null)" != "$tag" ]]; then
  rm -rf "$work/mesa"
  git clone -q --depth 1 --branch "$tag" "$url" "$work/mesa"
fi

toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64"
mkdir -p "$work/pkgconfig-empty"
cat > "$work/android-aarch64.ini" <<INI
[binaries]
ar = '$toolchain/bin/llvm-ar'
c = ['$toolchain/bin/aarch64-linux-android$api-clang']
cpp = ['$toolchain/bin/aarch64-linux-android$api-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-static-libstdc++', '-Wno-c++11-narrowing']
c_ld = 'lld'
cpp_ld = 'lld'
strip = '$toolchain/bin/llvm-strip'
pkg-config = ['env', 'PKG_CONFIG_LIBDIR=$work/pkgconfig-empty', '/usr/bin/pkg-config']

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
INI

rm -rf "$work/build"
# Vulkan only (no OpenGL), the KGSL kernel interface of Android's Adreno
# devices, and stubs for the Android system libraries.
meson setup "$work/build" "$work/mesa" --cross-file "$work/android-aarch64.ini" \
  -Dbuildtype=release -Db_ndebug=true -Dstrip=true \
  -Dplatforms=android -Dplatform-sdk-version=$api -Dandroid-stub=true -Dandroid-libbacktrace=disabled \
  -Dgallium-drivers= -Dvulkan-drivers=freedreno -Dvulkan-beta=true -Dfreedreno-kmds=kgsl \
  -Dopengl=false -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Dgbm=disabled \
  -Dllvm=disabled -Dxmlconfig=disabled -Dzstd=disabled -Dvalgrind=disabled -Dlibunwind=disabled
ninja -C "$work/build" src/freedreno/vulkan/libvulkan_freedreno.so
cp "$work/build/src/freedreno/vulkan/libvulkan_freedreno.so" "$out/"
"$toolchain/bin/llvm-strip" --strip-unneeded "$out/libvulkan_freedreno.so"
# Mesa's license (MIT), shipped with the driver.
cp "$work/mesa/docs/license.rst" "$out/turnip-license.rst"
python3 -I - "$out/turnip.json" "$tag" "$(git -C "$work/mesa" rev-parse HEAD)" <<'PY'
import json, sys
json.dump({"driver": "Turnip", "mesa": sys.argv[2], "commit": sys.argv[3],
           "library": "libvulkan_freedreno.so"}, open(sys.argv[1], "w"), indent=2)
PY
ls -la "$out"
