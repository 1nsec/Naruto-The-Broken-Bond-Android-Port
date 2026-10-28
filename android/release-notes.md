<!-- Notes of the GitHub release made by .github/workflows/android-apk.yml. -->
First alpha of the Android version of **Naruto: The Broken Bond PC Port v2.5**: the recompiled game with its native renderer on Vulkan, PC Options, a launcher and on-screen controls.

> **Alpha:** this is the first build to run on phones, so expect crashes and graphics problems. Logs from your phone help fix them (see below).

> **No game files are included.** You need your own Xbox 360 ISO of Naruto: The Broken Bond.

## Requirements

- Android 10 or newer, 64-bit
- **Snapdragon phone (Adreno 6xx or newer GPU)** with Vulkan 1.1. Mali (Exynos, most MediaTek) and PowerVR GPUs are not supported yet.
- 6 GB RAM or more recommended, and room for the ISO (about 7 GB)

## Install and play

1. Download the APK below and open it on your phone (allow installing apps from your browser or file manager when Android asks).
2. Open **Naruto: The Broken Bond** and tap **Choose game ISO…** to pick your ISO. Or copy the ISO into `Android/data/org.brokenbond.port/files/assets/`.
3. Check **Settings** (resolution, frame rate, on-screen controls), then tap **Play**. The first start takes a while.

The gear button on screen opens **PC Options**. Bluetooth and USB controllers work too, and the on-screen controls hide while one is connected. The back gesture opens a menu to continue, hide the controls or quit.

Saves, settings and logs are in `Android/data/org.brokenbond.port/files/`. A newer alpha installs over this one and keeps them, but **uninstalling the app deletes this folder:** back up `hdd/` first.

## Reporting problems

Open an issue with your phone model, Android version and the newest file in `logs/` (plus any `logs/crash_*.txt`).

Building it yourself, controls and known limitations: [docs/android.md](https://github.com/1nsec/Naruto-The-Broken-Bond-Android-Port/blob/main/docs/android.md).
