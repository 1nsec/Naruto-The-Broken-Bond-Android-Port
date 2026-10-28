# Android (alpha)

This repository builds the port as an Android app: the same recompiled game, its native renderer
on Vulkan, and the PC options menu, with a launcher and on-screen controls. It is an **alpha**:
the APK is built and packaged in CI, but it has not been tested on a phone yet, so expect crashes
and graphics problems. Logs from real devices are what moves it forward (see
[Reporting problems](#reporting-problems)).

> **No game files are included, and none are downloaded.** The game code is generated from your
> own `default.xex` and the shaders from your own `xeshader.sdb`, on your PC. The app reads your
> own Xbox 360 ISO from your phone's storage.

## Phone requirements

| | |
|---|---|
| Android | 10 or newer (API 29), 64-bit (arm64-v8a) |
| GPU | **Snapdragon (Adreno 6xx or newer)**, Vulkan 1.1 |
| Memory | 6 GB RAM or more recommended |
| Storage | The ISO (about 7 GB) plus about 1 GB for the app, saves and caches |

The renderer uses Xbox 360 texture formats as BC1-BC5 compressed textures and a 32-bit depth
buffer with stencil, which Adreno drivers support. **Mali (Exynos, most MediaTek) and PowerVR
GPUs are not supported yet**: they lack BC textures in Vulkan, so the game would draw wrong or not
start. The launcher warns when the phone does not report Vulkan 1.1.

## Build the APK (Windows)

The APK is built on the Windows PC that builds the PC version. You need everything from the
[build guide](build-and-run.md) up to section 6 (the SDK, your extracted game files, the generated
C++, Plume and XenosRecomp's dependencies), and also:

- **Android Studio** (it brings the Android SDK and a JDK). In **SDK Manager > SDK Tools**, check
  **Show Package Details** and install **NDK (Side by side) 29.0.14206865** and **Android SDK
  Command-line Tools**. In **SDK Platforms**, install **Android 15 (API 35)**.
- The **[Vulkan SDK](https://vulkan.lunarg.com)**, for `spirv-val`, which checks the SPIR-V
  shaders (or pass any `spirv-val.exe` with `-SpirvVal`).

Then, in PowerShell 7 at the repository root:

```powershell
./scripts/build-sdk.ps1     # if not done yet; it also applies patches/rexglue-android.patch
./scripts/build-game.ps1    # if not done yet: generates the C++ from game/default.xex
./scripts/build-android.ps1
```

The result is `dist/NarutoBrokenBond-Android-2.5-android-alpha.apk`. With a phone connected over
USB (developer options, USB debugging on), `-Install` also installs it with `adb`.

What the script does:

1. Checks `game/default.xex` (the supported revision), `game/xeshader.sdb` and the generated C++,
   applies the SDK patches (the PC ones and `rexglue-android.patch`, which only changes Android
   code), and fetches Plume's Vulkan submodules.
2. Builds the **SPIR-V shader cache** with the Windows game build, Vulkan backend only, in
   `build/android-shaders` (only its shader target). XenosRecomp, DXC and `spirv-val` only run on
   Windows, and the cache is the same for every platform. The first build translates every
   shader and takes a while; `build/shader-store` keeps them for later builds.
3. Cross-compiles the game (`libmain.so`), the ReXGlue runtime (`librexruntime.so`) and SDL3
   (`libSDL3.so`) with the NDK in `build/android`, and stages them with `libc++_shared.so` in
   `android/app/src/main/jniLibs/arm64-v8a`.
4. Packages the app with Gradle (`android/`) and copies the APK to `dist/`.

Options: `-PlumeSource` and `-XenosRecompThirdparty` (defaults `out/plume` and
`tools/xenosrecomp/thirdparty`, as in the build guide), `-SdkPrefix`, `-AndroidSdk` (default
`ANDROID_HOME`, then `%LOCALAPPDATA%\Android\Sdk`), `-Version`, `-Jobs`.

### Signing

Without a keystore the APK is signed with the Android debug key, which is enough to install it.
For a release key, set `BROKENBOND_KEYSTORE`, `BROKENBOND_KEYSTORE_PASSWORD`,
`BROKENBOND_KEY_ALIAS` and `BROKENBOND_KEY_PASSWORD` before running the script. Keep the
keystore out of Git (keep it outside the repository; `android/*.keystore` is ignored). An update must be signed with the same
key as the installed APK, or Android asks to uninstall it first (which deletes the saves in the
app's folder: back them up).

### CI

[`.github/workflows/android.yml`](../.github/workflows/android.yml) builds everything but the game
on every push: the SDK, the port's sources and the app, against stand-ins for the generated code
and the shader cache (`scripts/android/make-stub-generated.py`). Its APK
(`ci-apk-stub-game-code-NOT-PLAYABLE`) shows that the build and packaging work; it cannot play
anything.

## Using the app

1. Install the APK (allow installing from your file manager or browser when Android asks).
2. Open **Naruto: The Broken Bond** and tap **Choose game ISO…** to pick your ISO with the system
   file picker. The app keeps read access to it; nothing is copied. Or copy the ISO into
   `Android/data/org.brokenbond.port/files/assets/`, where the game finds it on its own, as on PC.
3. **Settings** has the options worth setting before the first start (render resolution, frame
   rate, anti-aliasing, language, on-screen controls). They are saved to `naruto.toml`; in game,
   PC Options has all of them.
4. Tap **Play**. The first start takes a while, and new areas can stutter once while their
   pipelines are created.

### Controls

- **On-screen controller:** touch the lower left of the screen for the left stick and the lower
  middle for the camera stick (each appears where you touch). A/B/X/Y are on the right, the D-pad
  on the upper left, LB/LT and RB/RT in the top corners, Back, the gear and Start at the top
  middle, and L3/R3 along the bottom edge. The **gear** opens **PC Options** (Back + Start).
  Opacity and size are in Settings.
- **Controllers:** Bluetooth and USB controllers work through SDL3 (Xbox, PlayStation, Switch Pro
  and most Android gamepads). The on-screen controller hides while one is connected.
- **Back gesture:** opens a menu to continue, hide or show the on-screen controls, or quit.

### Files

Everything the game writes is in `Android/data/org.brokenbond.port/files/`, the stand-in for the
PC version's game folder: `naruto.toml` (settings), `hdd/` (saves), `logs/` and caches. Back up
`hdd/` before uninstalling: Android deletes this folder with the app.

The defaults on Android (`android/app/src/main/assets/naruto.toml`) favor phones: 1x render
resolution, 30 FPS, no MSAA, FXAA or transparency anti-aliasing, and log files on.

## Reporting problems

Please include the phone model, the Android version and:

- the newest `logs/` file and any `logs/crash_*.txt` from the app's folder, or
- `adb logcat -s rexglue narutobb SDL DEBUG AndroidRuntime` while starting the game.

## Known limitations

- Untested on hardware so far: the first starts on real phones will find problems.
- Adreno only (see [Phone requirements](#phone-requirements)).
- Leaving the app pauses the game; switching back recreates the Vulkan surface, and this path is
  the least tested. Quitting from the back menu is the safest way out.
- The game runs in its own process (`:game`) and that process ends when the game closes.
- Keyboard-and-mouse options from the PC version are off by default; a Bluetooth keyboard works as
  on PC.
- Frame rates above 30 FPS depend heavily on the phone and its temperature.

## How the port works

- **SDK:** `patches/rexglue-android.patch` makes the ReXGlue runtime build with the NDK (Bionic):
  SDL3 as a shared library with its Android backend, an `ANativeWindow` surface for Vulkan,
  logging to logcat, arm64 fibers without `ucontext`, and replacements for what Bionic or the
  NDK's libc++ lack (robust mutexes, `std::jthread`, float `from_chars`). Code generation stays a
  host tool: `REXGLUE_HOST_TOOL` points the build at the PC's `rexglue.exe`.
- **Port:** the Windows APIs the port calls are implemented for POSIX in
  `src/platform/win32_posix.cpp` (files, events, timers, resources), the controller code uses SDL
  (`src/pc/pad_input_sdl.cpp`), and crashes are logged by `src/pc/crash_handler_posix.cpp`.
- **Renderer:** the native renderer's Vulkan backend presents to the Android window (RGBA
  swapchain, surface recreated after the app is paused); the D3D12 backend is not built.
- **App:** `android/` holds the launcher (`LauncherActivity`), the settings screen, the game
  activity (SDL3's `SDLActivity`, which loads `libmain.so`) and the on-screen controller, which
  is an SDL virtual gamepad (`src/android/touch_controller.cpp`).
