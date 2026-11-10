<!-- Notes of the GitHub release made by .github/workflows/android-apk.yml. -->
Beta of the Android version of **Naruto: The Broken Bond PC Port v2.5**: the recompiled game with its native renderer on Vulkan, PC Options, a launcher and on-screen controls.

**Android port by [1nsec](https://github.com/1nsec).** The official page is [github.com/1nsec/Naruto-The-Broken-Bond-Android-Port](https://github.com/1nsec/Naruto-The-Broken-Bond-Android-Port): copies on other sites are re-uploads.

> **Beta:** the game plays at 30 FPS on a Snapdragon 865 handheld (Retroid Pocket 5), the open world included, but it has been tested on few devices. Expect the odd crash, graphics problem or stutter; logs from your phone help fix them (see below).

> **No game files are included.** You need your own Xbox 360 ISO of Naruto: The Broken Bond.

## New in the beta

- **New launcher:** parchment cards with red-seal choices, in the style of the on-screen controls, over an orange and a blue chakra that drift and glow. Play and the disc on the left, with who made the port and his YouTube and Instagram; on the right, the graphics preset, render resolution, screen and frame rate in one tap, and shortcuts to Settings, DLC, Help, logs and GitHub. A choice goes up in a puff of smoke and comes back as a red seal; Play makes a drummer's call and a drum while kunai and shuriken are thrown at it. It has its own music, a theme written for the port (flute, koto and taiko), and its own sounds, all original; Settings turns them off, and can play any song on your phone instead of the theme.
- **DLC:** the launcher's **DLC** button adds the game's downloadable content from your own Xbox 360 (the package files of `Content/0000000000000000/55530825/00000002/`); the game installs them at its next start.
- **Your own GPU drivers:** Settings > GPU driver > **Add driver (.zip)** installs other Turnip builds (the adrenotools packages published for Android emulators), next to the Turnip the app carries. Snapdragon (Adreno) only.
- **Auto-rotate:** the game turns with the phone between the two landscape sides.
- The launcher shows who made the Android port (**1nsec**), and its GitHub button has an icon.
- **Full screen:** Settings > **Screen** > *Full screen* (or PC Options > Display > Aspect ratio > Automatic) fills a phone's wide screen with a wider view of the game instead of black bars on the sides.
- **The picked ISO keeps working:** on some phones the game could not open the ISO again in a later session and went straight back to the launcher, so it had to be picked every time.
- **Camera on the on-screen controls:** no more camera stick. Drag a finger anywhere on the screen (outside the buttons and the left stick) to turn the camera, as in mobile games. Its sensitivity is in Settings and in PC Options > Controls.
- **Performance mode:** Settings > **Water reflections**, **Character shadows** and **Draw distance** (Full, Medium or Short: small objects far from the camera are not drawn). They also change **right away** from PC Options > Graphics, to compare in the same place. The presets set them: Performance (540p, no reflections or shadows, short draw distance), Balanced (720p, medium draw distance) and Quality (1080p).
- **Sound in fights and cutscenes:** the game's 5.1 sound is folded to stereo by the app, as on a PC with stereo speakers, instead of by Android, which lost it on some devices.
- **Faster:** the game's shaders read their constants the way Adreno GPUs handle best, the GPU gets its work earlier and in fewer passes, no thread of the app wastes a core, and the performance log costs little CPU.
- A **GitHub** button in the launcher opens the project's page, for new versions and problem reports.
- The bundled Turnip driver is newer (Mesa 26.1).
- PC Options offers render resolutions up to 1080p on phones; a larger value left in naruto.toml is capped at 1440p instead of running out of memory.

## Since the first alphas

- PC Options, the FPS counter and the PC title screen show up on Android.
- A built-in or connected controller (Retroid Pocket, Bluetooth, USB) is **player 1**; the on-screen controller hides while one is connected.
- **Fewer stutters:** graphics the driver is still preparing are skipped for a moment instead of freezing the game (Settings > Skip unready graphics), and prepared graphics are kept for the next sessions, so an area stutters only the first time it is played.
- The game always fills the screen, and cutscenes no longer flash black while the game is slow to show a frame.
- Settings > **Performance log** (on by default during the beta): frame times, a CPU profile and images of the frame's passes, which **Save logs** includes.
- Settings > **Turnip rendering** (experimental): *Automatic*, *Direct (sysmem)* or *Tiled (gmem)*.

## Requirements

- Android 10 or newer, 64-bit
- **Snapdragon phone (Adreno 6xx or newer GPU)**. Mali (Exynos, most MediaTek) and PowerVR GPUs are not supported yet. Where the phone's own driver lacks a feature (Snapdragon 865 and older), the app uses the Turnip driver it carries.
- 6 GB RAM or more recommended, and room for the ISO (about 7 GB)

## Install and play

1. Download the APK below and open it on your phone (allow installing apps from your browser or file manager when Android asks).
2. Open **Naruto: The Broken Bond** and tap **Choose game ISO…** to pick your ISO. Or copy the ISO into `Android/data/org.brokenbond.port/files/assets/`.
3. Check **Settings** (resolution, frame rate, on-screen controls), then tap **Play**. The first start takes a while.

The gear button on screen, or **Back + Start** on a controller, opens **PC Options**. The on-screen controls hide while a controller is connected. The back gesture opens a menu to continue, hide the controls or quit.

Saves, settings and logs are in `Android/data/org.brokenbond.port/files/`. A newer version installs over this one and keeps them, but **uninstalling the app deletes this folder:** back up `hdd/` first.

## Reporting problems

In the launcher, tap **Save logs for a problem report** and open an issue with the zip it saves in Downloads and a few words about what happened.

Building it yourself, controls and known limitations: [docs/android.md](https://github.com/1nsec/Naruto-The-Broken-Bond-Android-Port/blob/main/docs/android.md).
