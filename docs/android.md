# Android (alpha)

This repository builds the port as an Android app: the same recompiled game, its native renderer
on Vulkan, and the PC options menu, with a launcher and on-screen controls. It is an **alpha**:
it starts and runs on a Retroid Pocket 5 (Snapdragon 865, with the bundled Turnip driver), but
expect crashes, graphics problems and slowdowns. Logs from real devices are what moves it forward
(see [Reporting problems](#reporting-problems)).

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

The renderer needs Vulkan 1.2 with 64-bit integers in shaders (the shader cache reads its data
through buffer addresses), BC1-BC5 compressed textures (the Xbox 360 formats) and a 32-bit depth
buffer with stencil. The Qualcomm driver of older Adreno GPUs, such as the Adreno 650 of the
Snapdragon 865 (Retroid Pocket 5, Galaxy S20...), lacks some of these, so the app bundles
**Turnip**, Mesa's open-source Vulkan driver for Adreno 6xx and 7xx GPUs, and loads it with
[libadrenotools](https://github.com/bylaws/libadrenotools) when the system driver falls short.
Turnip is a driver, not an emulator: the game still runs natively.

**Settings > GPU driver**: *Automatic* (default) checks the system driver and takes Turnip on a
Qualcomm GPU that lacks a feature; *System* or *Turnip* force one. The game's log says which
driver ran and what the system driver offered.

**Mali (Exynos, most MediaTek) and PowerVR GPUs are not supported yet**: they lack BC textures in
Vulkan, and Turnip only runs on Adreno. The launcher warns when the phone does not report
Vulkan 1.1.

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

Without a private keystore, release APKs are signed with the alpha key in
`android/signing/public-alpha.keystore`. It is public on purpose: every build made without a
private key, on your PC or in the cloud, has the same signature, so a newer APK installs over an
older one and keeps the saves. Since anyone can sign with it, switch to a private key for stable
releases: set `BROKENBOND_KEYSTORE`, `BROKENBOND_KEYSTORE_PASSWORD`, `BROKENBOND_KEY_ALIAS` and
`BROKENBOND_KEY_PASSWORD` before running the script, and keep that keystore out of Git. An APK
signed with another key does not install over the old one: Android asks to uninstall it first,
which deletes the saves in the app's folder (back them up).

### CI

[`.github/workflows/android.yml`](../.github/workflows/android.yml) builds everything but the game
on every push: the SDK, the port's sources and the app, against stand-ins for the generated code
and the shader cache (`scripts/android/make-stub-generated.py`). Its APK
(`ci-apk-stub-game-code-NOT-PLAYABLE`) shows that the build and packaging work; it cannot play
anything.

## Cloud build (without your PC)

[`.github/workflows/android-apk.yml`](../.github/workflows/android-apk.yml) runs the same steps
as `build-android.ps1` on a Windows machine of GitHub's and publishes the APK as a release. It
needs your game files once, through a private link:

1. Upload **either** your ISO **or** a `.zip`, `.rar` or `.7z` with `default.xex` and `xeshader.sdb` (from the
   extracted game) to Google Drive or Dropbox, and share it as "anyone with the link".
2. In the repository on GitHub: **Settings > Secrets and variables > Actions > New repository
   secret**, name `GAME_FILES_URL`, value: the link. Secrets are hidden, also in the build logs.
3. **Actions > Android APK > Run workflow**: choose `apk`, the version, and `draft` (only you see
   the release until you publish it) or `prerelease` (public right away).

The build takes one to a few hours. The workflow takes only `default.xex` and `xeshader.sdb`
(from the ISO without extracting the rest), checks that `default.xex` is the supported revision,
and builds with them on a machine that GitHub deletes after the run. Nothing derived from them is
committed or cached; only the APK is uploaded. You can delete the link and the secret afterwards.

The `test` mode builds the same way against stand-ins, without the secret, to check the
workflow; its APK cannot play anything.

## Using the app

1. Install the APK (allow installing from your file manager or browser when Android asks).
2. Open **Naruto: The Broken Bond** and tap **Choose game ISO…** to pick your ISO with the system
   file picker. The app keeps read access to it; nothing is copied. Or copy the ISO into
   `Android/data/org.brokenbond.port/files/assets/`, where the game finds it on its own, as on PC.
3. **Settings** has the options worth setting before the first start (render resolution, frame
   rate, anti-aliasing, language, on-screen controls). They are saved to `naruto.toml`; in game,
   PC Options has all of them.
4. Tap **Play**. The first start takes a while, and new areas can stutter the first time they are
   played while their pipelines are created (see [Performance](#performance)).

### Controls

- **On-screen controller:** touch the lower left of the screen for the left stick and the lower
  middle for the camera stick (each appears where you touch). Each button shows its action, with
  the pad button in a small badge: the big **X** (attack) at the lower right with **A** (jump),
  **Y** (heavy attack), **B** (block) and **RB** (kunai) around it and **RT** (sprint) above;
  **LT** (jutsus) and **LB** (inventory) on the upper left under the health bar, with the D-pad
  below them; Back, the gear and Start at the top middle. The **gear** opens **PC Options**
  (Back + Start). Opacity and size are in Settings. The icons are
  [Material Symbols](https://github.com/google/material-design-icons) (Apache License 2.0; the
  license is in the app's `assets/licenses`).
- **Controllers:** Bluetooth and USB controllers, and the controls built into handhelds such as the
  Retroid Pocket, work through SDL3 (Xbox, PlayStation, Switch Pro and most Android gamepads).
  The on-screen controller hides while one is connected and leaves the game's controller list, so
  a physical controller is always player 1. **Back + Start** opens PC Options.
- **Back gesture:** opens a menu to continue, hide or show the on-screen controls, or quit.

### Performance

- **Skip unready graphics** (Settings, on by default; PC Options > Graphics): a draw whose graphics
  pipeline the driver is still compiling is left out of its frame for a moment and the pipeline is
  compiled on other cores, instead of the frame waiting for it. Phone drivers compile slowly, so
  without it every new effect freezes the game for a moment.
- **Pipelines kept between sessions:** the renderer creates every pipeline through a Vulkan
  pipeline cache saved in the app's private storage (`files/shader-cache/vulkan-pipelines.bin`,
  every 20 seconds while new ones are made, and when the game closes). The next sessions make the
  same pipelines from it in a fraction of the time, so an area stutters only the first time it is
  played. A file made by another driver (Settings > GPU driver) is ignored and replaced. Mesa's own
  disk cache for Turnip is pointed at `files/shader-cache/mesa`.
- **Pipelines prepared ahead:** with the cache, the renderer keeps the list of the pipelines drawn
  on the device (`files/shader-cache/pipelines.list`, in the order of their first draw, with the
  area each was first drawn in). The next sessions prepare them in the background from the start,
  and those of an area while its loading screen is up, as the PC version does with the list built
  from its test boots (the APK is built without one).
- **Performance log** (Settings): writes `pipeline-runs/<time>/performance.csv` in the data folder
  (one line per second: frame times, stutters, where the render thread spent its time, what the
  pipelines did). **Save logs for a problem report** includes the newest three.

### Files

Everything the game writes is in `Android/data/org.brokenbond.port/files/`, the stand-in for the
PC version's game folder: `naruto.toml` (settings), `hdd/` (saves), `logs/` and caches. Back up
`hdd/` before uninstalling: Android deletes this folder with the app.

The defaults on Android (`android/app/src/main/assets/naruto.toml`) favor phones: 1x render
resolution, 30 FPS, no MSAA, FXAA or transparency anti-aliasing, and log files on.

## Reporting problems

In the launcher, tap **Save logs for a problem report**. It saves
`NarutoBrokenBond-logs-<date>.zip` in **Downloads** with the device's details (model, Android
version, Vulkan level), `naruto.toml`, the newest files of `logs/` (game logs and
`crash_*.txt`) and the app's own system log, which also covers what happened before the game's
log started. Attach it to a GitHub issue with a few words about what happened.

With a PC, `adb logcat -s rexglue narutobb SDL DEBUG AndroidRuntime` while starting the game shows
the same live.

## Known limitations

- Tested on few devices so far (a Retroid Pocket 5): other phones will find other problems.
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
  swapchain, surface recreated after the app is paused) and draws the PC overlay (PC Options, the
  FPS counter, the PC title screen) over each frame (`src/gpu/backend/vulkan/overlay.inc`, with
  `src/gpu/backend/overlay_pass.hlsl` built into the shader cache), and black frames under it while
  the game shows nothing; the D3D12 backend is not built.
- **App:** `android/` holds the launcher (`LauncherActivity`), the settings screen, the game
  activity (SDL3's `SDLActivity`, which loads `libmain.so`) and the on-screen controller, which
  is an SDL virtual gamepad (`src/android/touch_controller.cpp`).
