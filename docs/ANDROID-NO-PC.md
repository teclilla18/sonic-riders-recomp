# Android, no PC needed (beta)

Install **Riders Rebuilt** on an Android phone without a computer. The phone builds the game itself from your
own disc image. This is a beta: it has been tested on a Galaxy S23 Ultra so far.

## What you need

- A **64-bit** Android phone with **Vulkan 1.1** (most phones from 2019 on), Android 11 or newer.
- About **2 GB** of free space (the disc image is copied into the app).
- Your own **Sonic Riders (USA)** disc image (`GXEE8P`, a full `.iso`) on the phone.

## Install

1. Download `RidersRebuilt-Android-NoPC-<version>.zip` from the [latest release](https://github.com/teclilla18/sonic-riders-recomp/releases/latest)
   and open it. Inside is the app (`.apk`) and its source code (`-source.zip`, you don't need it to play).
2. Tap the `.apk` and allow the install (Android asks once to allow installs from your browser or file app).
3. Open **Riders Rebuilt**, tap **Choose an ISO** and pick your disc image. The app checks that it is the right game.
4. Tap **Build game**. Keep the app open: the phone builds the game in about **5 minutes**.
5. Tap **PLAY**.

## Updating

Install the new `.apk` over the old one. Your disc image, saves, settings and mods stay. If the update
changes the game, the app shows **Prepare this version**: tap **Build update** (a few minutes) before you play.

## If something goes wrong

- **The build stopped** (the app was closed or the phone ran out of battery): open the app again: it shows
  **Continue installation**; tap **Continue**. Your disc image is kept.
- **"Not enough space"**: free up space and try again.
- **"Requires a 64-bit ARM device" / "Vulkan 1.1"**: this phone can't run it; use the
  [regular Android setup](INSTALL.md#install-on-android) on a PC instead.
- Anything else: in the app, copy the **diagnostic report** and paste it into a
  [bug report](https://github.com/teclilla18/sonic-riders-recomp/issues/new/choose).

Online play works here too: see [Play online](PLAY-ONLINE.md).
