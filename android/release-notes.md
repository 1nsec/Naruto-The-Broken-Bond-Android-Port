<!-- Notes of the GitHub release made by .github/workflows/android-apk.yml. -->
Alpha of the Android version of **Naruto: The Broken Bond PC Port v2.5**: the recompiled game with its native renderer on Vulkan, PC Options, a launcher and on-screen controls.

> **Alpha:** expect crashes, graphics problems and slowdowns. Logs from your phone help fix them (see below).

> **No game files are included.** You need your own Xbox 360 ISO of Naruto: The Broken Bond.

## New in this alpha

- **PC Options, the FPS counter and the PC title screen now show up on Android.** The Vulkan renderer did not draw them before, which left a black screen with the title music.
- A built-in or connected controller (Retroid Pocket, Bluetooth, USB) is **player 1**; the on-screen controller no longer takes its place.
- **Fewer stutters:** graphics the driver is still preparing are skipped for a moment instead of freezing the game (Settings > Skip unready graphics), and prepared graphics are kept for the next sessions, so an area stutters only the first time it is played.
- The game always fills the screen (no status or navigation bar).
- **Graphics presets** in Settings: Performance (540p), Balanced (720p) and Quality (1080p). The render resolution now goes below 720p on phones (the 540p choice used to stay at 720p).
- Settings > **Performance log** (on by default during the alpha): records the frame times, so that a problem report shows where the game slows down.
- Cutscenes no longer flash black with the FPS counter on: while the game is slow to show a frame, the last image stays on screen.
- **Faster GPU work:** the game's shaders read their constants the way Adreno GPUs handle best, and data uploaded in the middle of a frame no longer interrupts its drawing.
- Settings > **Turnip rendering** (experimental): *Automatic*, *Direct (sysmem)* or *Tiled (gmem)*. Try them in the open world and tell us which one runs faster.
- The performance log also records a CPU profile and images of the frame's passes, which **Save logs** includes.

## Requirements

- Android 10 or newer, 64-bit
- **Snapdragon phone (Adreno 6xx or newer GPU)**. Mali (Exynos, most MediaTek) and PowerVR GPUs are not supported yet. Where the phone's own driver lacks a feature (Snapdragon 865 and older), the app uses the Turnip driver it carries.
- 6 GB RAM or more recommended, and room for the ISO (about 7 GB)

## Install and play

1. Download the APK below and open it on your phone (allow installing apps from your browser or file manager when Android asks).
2. Open **Naruto: The Broken Bond** and tap **Choose game ISO…** to pick your ISO. Or copy the ISO into `Android/data/org.brokenbond.port/files/assets/`.
3. Check **Settings** (resolution, frame rate, on-screen controls), then tap **Play**. The first start takes a while.

The gear button on screen, or **Back + Start** on a controller, opens **PC Options**. The on-screen controls hide while a controller is connected. The back gesture opens a menu to continue, hide the controls or quit.

Saves, settings and logs are in `Android/data/org.brokenbond.port/files/`. A newer alpha installs over this one and keeps them, but **uninstalling the app deletes this folder:** back up `hdd/` first.

## Reporting problems

In the launcher, tap **Save logs for a problem report** and open an issue with the zip it saves in Downloads and a few words about what happened.

Building it yourself, controls and known limitations: [docs/android.md](https://github.com/1nsec/Naruto-The-Broken-Bond-Android-Port/blob/main/docs/android.md).
