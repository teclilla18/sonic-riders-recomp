# iOS port: research and plan (2026-11-07)

Status (2026-11-07): **first boot done**: iPhone 17 Pro, iOS 27.0, free account: intro movie and title screen at
60 fps, sound mixing, log and screenshots pulled [confirmed, scripted START]. Not yet: touch/pad play, a race,
background/resume, iPad, the 7-day reinstall. Build/test commands and first-boot findings: end of this note.

Target hardware for testing: an Apple-silicon Mac with current Xcode, an iPhone on iOS 27, a **free Apple account**
(Personal Team). Facts below marked [checked] were checked on that Mac on 2026-11-07; the rest is from Apple /
SDL / MoltenVK documentation and is [likely] until a build proves it.

## Summary and recommendation

Build the iOS app from the **Android code path**: the SDL2 platform layer (`platform/host/`) plus the Vulkan
renderer (`platform/android/render_vk.c`) running on **MoltenVK** (Vulkan on Metal), and the Android touch
controls unchanged. Static recompilation means no JIT, so nothing in Apple's no-executable-memory rule touches us.
The new code is small: a `<threads.h>` shim, a few `__APPLE__` branches (paths, thread priority, logging), the
build target, an app bundle and signing. A native Metal renderer is not worth it until profiling says so.

Data rule: the app contains recompiled game code, so **an .ipa can never be published**. Like the PC setup, every
player builds it on their own Mac from their own disc. On the developer Mac the whole setup runs locally (dtk has
a macOS arm64 build), so only the disc image has to come from elsewhere, never `build/`.

## How the Android / Linux host build maps to iOS

| Part | Android today | iOS |
|---|---|---|
| Entry, window, events | SDL2 (`SDL_main` from SDLActivity) | SDL2 UIKit backend: `SDL_main` from `SDL_uikit_main`; same events |
| Platform glue | `host/platform_host.c` (`__ANDROID__` branches) | same file, `__APPLE__` + `TARGET_OS_IOS` branches |
| Renderer | `android/render_vk.c` via `SDL_Vulkan_*` | same file on MoltenVK (static `MoltenVK.xcframework`); see below |
| Shaders | GLSL → SPIR-V with the NDK's glslc, embedded | same SPIR-V (glslc from Homebrew `shaderc`); MoltenVK turns it into MSL at pipeline creation |
| Touch controls | `android/touch_android.c` (SDL finger events) | the same file; add safe-area insets (Dynamic Island, home bar) |
| Pads | `host/input_sdl.c` | the same; SDL2 on iOS reads GameController.framework (Xbox, DualSense, Switch Pro, MFi) |
| Audio | `host/audio_sdl.c` | the same (SDL's CoreAudio backend sets up AVAudioSession) |
| Threads | pthreads + C11 `<threads.h>`; fast cores + nice (#14) | pthreads; **the iOS SDK has no `<threads.h>`** [checked: 14 runtime files use it] → small header shim on pthreads; QoS classes instead of affinity |
| Guest memory | `mmap` 512 MiB reserve + `mprotect` | the same code (`memory.c` non-Windows path); 512 MiB of reserved address space is fine on iOS |
| Data folder | `SDL_AndroidGetExternalStoragePath()`, `chdir` | `$HOME/Documents`, `chdir` (visible in the Files app, see below) |
| Logs | file + logcat | file `Documents/sonicriders.log` + stderr/os_log (seen by `devicectl --console` and Console.app) |
| Crash report | signal handler → crash.txt (`crash_signal`) | the same handler (signals and `dladdr` work); iOS's own crash logs too |
| Background | `SDL_APP_WILLENTERBACKGROUND` in `render_vk.c` | the same events; iOS forbids GPU work in the background, so the pause must be complete |
| Packaging | `tools/android_build.py` (APK without Gradle) | ninja builds the program; a generated Xcode project only links, bundles and signs (below) |

`render_vk.c` and `touch_android.c` would move or be shared under a neutral name later (e.g. `platform/vk/`);
for a first build they compile from where they are.

## Renderer: MoltenVK (recommended) vs native Metal vs GL

- **MoltenVK** (KhronosGroup release v1.4.2, 2026-07-24, asset `MoltenVK-ios.tar`): reuses the whole Vulkan
  renderer (~2000 lines, pipeline cache, prewarm, specialized shaders, overlay, touch drawing). What
  `render_vk.c` needs and MoltenVK provides on Apple GPUs: dynamic rendering (core 1.3), push descriptors,
  depth-stencil resolve, `dualSrcBlend`, point and line lists. Things to handle:
  - Enable `VK_KHR_portability_subset` on the device when it is listed (the spec requires it); with the loader,
    also `VK_KHR_portability_enumeration` + the instance flag. Linking MoltenVK statically needs no loader.
  - First-use hitches: each new pipeline is SPIR-V → MSL → Metal compile. The existing pipeline cache + prewarm
    covers this as on Android; check the cache survives restarts (it is a file in the data folder).
  - Present: FIFO on a `CAMetalLayer`. iPhone 17 Pro is ProMotion (120 Hz); the game paces itself to 60.
- **Native Metal**: a third renderer to keep in step with deko3d and Vulkan, and every GX shader fix done three
  times. Only if MoltenVK profiling shows a real cost.
- **OpenGL ES**: deprecated on iOS since 12, and `render_gl.c` is desktop GL. No.

## No JIT needed

iOS refuses writable+executable memory to apps (outside debugger tricks). The game is recompiled ahead of time
into C, so the app is ordinary native code. The runtime has no `PROT_EXEC`, `fork`, `system` or `popen`
[checked]. Gecko / `[memory]` mods only write data, so they keep working.

## Signing with a free Apple account

- Xcode → Settings → Accounts: add the Apple ID once; it makes an "Apple Development" certificate and a Personal
  Team. Builds signed this way **expire after 7 days** (reinstall from the Mac; the Documents folder, disc image
  and saves survive a reinstall over the old app, not a delete).
- Limits: 3 sideloaded apps per device, 10 new App IDs per 7 days. The bundle ID must be unique across Apple, so
  it gets a local suffix (`--bundle-id`, kept in NOTES.local.md, never committed).
- iPhone: Settings → Privacy & Security → **Developer Mode** on (restart), and after the first install trust the
  developer under Settings → General → VPN & Device Management.
- Free teams cannot use most capabilities (iCloud, push, app groups, increased memory limit). None are needed:
  the game uses tens of MB of guest RAM plus textures.
- Plan: ninja builds `libsonicriders.a` (runtime + recompiled code) with `xcrun -sdk iphoneos clang`; configure.py
  writes a tiny Xcode project in `build/ios/` that links it with SDL2 and MoltenVK and lets Xcode do the free-team
  provisioning: `xcodebuild -allowProvisioningUpdates DEVELOPMENT_TEAM=<team>`. Install and run with
  `xcrun devicectl device install app` / `device process launch --console` (the iOS version of `adb`).
  Signing by hand with `codesign` needs a provisioning profile that only Xcode makes for a free team, so it is
  not simpler.

## Files: disc image, saves, logs

- `Info.plist`: `UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace` → the app's Documents folder shows in
  the Files app ("On My iPhone → Sonic Riders") and in Finder (iPhone → Files) over USB.
- The disc image goes into Documents once (~1.4 GB): from the Mac with Finder, or
  `xcrun devicectl device copy to --domain-type appDataContainer --domain-identifier <bundle id>`. Not inside the
  app bundle (every reinstall would copy 1.4 GB, and it would be in the signed bundle).
- Saves (`saves/`), settings, shader keys, pipeline cache and `sonicriders.log` live next to it, as on Android.
  `devicectl device copy from` pulls the log back; the player can share it from the Files app.
- Later: a document picker (open the image where it is, e.g. iCloud Drive) via `UIDocumentPickerViewController`
  and a security-scoped bookmark. Not for the first build.

## Touch controls and controllers

- Touch: reuse `touch_android.c` and the Android layout (accepted on the S23, 2026-11-03). The S23 is ~19.5:9,
  the iPhone 17 Pro ~19.5:9 too, so positions carry over. New: keep buttons out of the safe-area insets (Dynamic
  Island side in landscape, home indicator) and set `SDL_HINT_IOS_HIDE_HOME_INDICATOR` = 2 so a swipe on the edge
  does not leave the game on the first try.
- Controllers: SDL2's iOS joystick driver uses GameController.framework, so Xbox Series, DualSense / DualShock 4,
  Switch Pro and MFi pads work over Bluetooth with the same `input_sdl.c` mapping (triggers as axes). Joy-Cons in
  pairs only as iOS presents them. Touch controls hide when a pad is used, as on Android.
- iPadOS also delivers pad buttons as UIKit presses that SDL turns into keys (A/Cross = Select = Enter = our
  START, B = Escape, D-pad = arrows): A paused races (2026-11-09). `input_pump` ignores the keyboard on iOS while a
  controller is open.

## Performance expectations

A19 Pro (iPhone 17 Pro) CPU cores are well ahead of the S23 Ultra's, which already runs 1P at ~59-60 fps and the
game at ~2 ms/step [likely: 60 fps is easy]. Thermal throttling in long sessions is the thing to watch. No
thread affinity on iOS; the game, render and audio threads get `QOS_CLASS_USER_INTERACTIVE`.

## Steps

1. Mac setup (one time): Homebrew `ninja shaderc`; download pinned (SHA-256 in `setup/toolchain.json`):
   `dtk-macos-arm64` v1.8.4, `MoltenVK-ios.tar` v1.4.2, SDL2 2.32.10 source; run the dtk split + recompiler
   from the user's disc image (same as `tools/setup_linux.py`).
2. Runtime: `<threads.h>` shim for Apple; `__APPLE__` branches in platform_host.c (Documents, QoS, log),
   portability-subset in render_vk.c, safe area in the touch layout.
3. `configure.py --target ios --team <id> --bundle-id <id>`: ninja static lib + generated Xcode project; SDL2 built
   for iOS from source.
4. First boot: install, copy the image into Documents, launch with `--console`; goal = title screen, log readable.
5. Then: pad + touch on the phone, a 1-lap race, frame rate, background/resume, 7-day reinstall check.

## Build and test on the Mac (2026-11-07)

- Setup by hand (no Mac setup script yet): copy the disc image to `baserom/` (a copy, never a link),
  `python3 -I tools/extract_iso.py baserom/<image>.iso baserom`, `build/ext/dtk/dtk dol split config/GXEE8P/config.yml
  build/GXEE8P`; `configure.py --target ios` recompiles into build/gen when it is empty.
- Signing: a free account has no certificate until a team is picked once: open `build/ios/xcode/SonicRiders.xcodeproj`,
  Signing & Capabilities, Team = Personal Team; then configure finds the team itself. First launch: unlock the phone,
  Settings > General > VPN & Device Management > Trust.
- `python3 configure.py --target ios [--icon <square.png>]`, `ninja`, `tools/ios_run.py install`, `push-iso` (once),
  `run SR_INPUT=1500:START,1510:-START SR_SCREENSHOT=2200` (title screen), `pull sonicriders.log shot_2200.bmp`.
- Every `ninja` also makes `build/ios/SonicRiders.ipa` and copies it to `~/Desktop/Sonic Riders iOS/SonicRiders.ipa`
  (previous build: `SonicRiders-prev.ipa`; `--ipa-dir <dir>` or `none`). Reinstall with Sideloadly/AltStore when the
  7-day signature runs out. Sideloadly re-signs it; if it changes the bundle ID, the app gets a new Documents folder
  (copy the disc image again). Never share the .ipa (recompiled game code); `.ipa` is in .gitignore and the guard.
- A crash before the log opens: `xcrun devicectl device info files --device <id> --domain-type systemCrashLogs`,
  then `device copy from ... --domain-type systemCrashLogs --source SonicRiders-<date>.ips`.

First-boot findings [confirmed on the iPhone]:
- iOS 27 kills an app without the UIScene life cycle at launch (SIGTRAP in
  `_UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption`). SDL2 has no scene code: Info.plist declares one
  window scene and `platform/ios/ios_scene.m` (`ios_attach_window`) moves SDL's UIWindow onto it.
- SDL2's UIKit video code needs the OpenGLES framework at link time (EAGL), even with Vulkan.
- Without `SDL_WINDOW_ALLOW_HIGHDPI` the swapchain is in points (874x402) and iOS stretches it 3x: jagged touch
  controls. With it: 2622x1206, still 60 fps on the title screen.
- MoltenVK 1.4.2 reports "Apple A19 Pro GPU, Vulkan 1.3"; all 1167 states used specialized shaders in the intro.
- Screenshot BMPs needed 4-byte row padding (874 * 3 is not a multiple of 4); fixed in render_vk.c for all targets.
- Background: iOS refuses GPU work from a background app (MoltenVK: device lost; rebuilding then crashed). The
  render loop waits in `ios_wait_in_background` from "will resign active" until active again, and holds the game
  thread (`rt_host_pause`, at frame end) and the audio output (`platform_audio_pause`): Control Center only makes
  the app inactive, so without that the race and music ran on unseen. Test without
  touching the phone: `xcrun devicectl device process launch --device <id> com.apple.Preferences`, then launch the
  game's bundle ID again (no --terminate-existing).
- Touch input and the phone menu share Android's code: `SR_IOS` next to `__ANDROID__` (input_sdl.c, menu.cpp `SR_PHONE`).
- Icon: `--icon <png>` (flattened onto blue, 1024x1024 asset catalog). Fan art stays local, never committed.


## Release (v0.7.0 onward)

- Players: `SonicRiders-iOS-Setup-<ver>.zip` (`tools/make_release.py --flavor ios`): "Install Sonic Riders
  (iOS).command" runs `tools/setup_mac.py` (7 steps: disc check, Xcode + Metal Toolchain + Homebrew ninja/shaderc
  + Apple ID, pinned downloads from setup/toolchain.json "mac", extract, dtk, configure + ninja, install).
  Apple silicon only; the folder path must have no spaces. Docs: INSTALL.md "Install on iPhone / iPad".
- Signing: `ios_build.find_team()` = the Apple Development certificate, else `defaults read com.apple.dt.Xcode
  IDEProvisioningTeamByIdentifier` (an Apple ID added in Xcode, no manual project step). No team or `--team none`:
  an unsigned app/.ipa (CODE_SIGNING_ALLOWED=NO) that Sideloadly / AltStore sign. Bundle ID: per-team suffix;
  unsigned = `net.srnative.sonicriders`; Sideloadly appends the team ID (a new app, empty Documents, the first time).
- Disc: `platform_disc_path` also looks for `*.iso` at the top of Documents (Finder's Files tab puts it there).
  main.c (SR_IOS): no image -> a message; smaller than 1,459,978,240 bytes -> "incomplete" (opened mid-copy hung
  at the loading icon). Messages before the game's window need a window attached to the scene
  (`ios_attach_window`, ios_scene.m), else UIKit's alert waits unseen on black.
- Still copying (ef6c112): a disc image changed in the last 2 min is stat'ed twice 1.5 s apart; size/date moved
  -> "still copying" message. Finder's Files tab writes the file growing from 0 [confirmed: devicectl copy on the
  iPad Pro, Finder copy on the A16 iPad, user]. After deleting + reinstalling the app, Finder's Files tab shows
  the old folder ("The required file cannot be found") until Finder is refreshed (click away and back).
- App icon: `--icon <picture>` (PNG, JPG, HEIC, WEBP, GIF, BMP, TIFF: `ios_build.ICON_EXTS`), else the save icon from
  baserom/_Main.rel (make_icon.decode_icon, 32x nearest). `ios_build.square_png`: centre square with sips (two
  calls: in one, sips scales before it crops), 1024 px; `flatten_png` fills transparency with ICON_BACKGROUND
  (8/16-bit, grey/RGB/palette). `ios_build.py icon <in> <out.png>` = the same conversion (the launcher's preview).
  setup_mac.py copies the picture to build/ios/custom_icon.<ext> (never git; a path without spaces for ninja).
- A free Apple ID: 3 sideloaded apps per device (MIInstallerErrorDomain 13), 7-day signatures.
- Renewal: renew the same way as installed. Sideloadly on a Mac-installed app appends the team ID (a second,
  empty app) unless Advanced Options > "Use automatic bundle ID" is unticked (keeps the .ipa's ID): tested on the iPad Pro 2026-11-08,
  one app kept, disc + saves kept [confirmed]; Sideloadly stored info_props CFBundleIdentifier =
  net.srnative.sonicriders.t<hash>. setup_mac.py writes "How to renew (every 7 days).txt" next to the .ipa with
  that ID; a re-run asks separately about copying the disc image.
- Cold test: extract the zip with `ditto -x -k` into a fresh folder, run the .command with `SR_NOPAUSE=1 --yes
  --unsigned --iso <img> --tools <fresh dir> --ipa-dir <scratch>` (never the Desktop folder). PASS 2026-11-08.
  Not covered: a Mac without Homebrew or Xcode; the signed install route from the zip.

## Pause, clock and touch (2026-11-08)

- `rt_host_pause` (frame_pacing.c) also calls `rt_clock_hold` (cpu.c): the time base stands still while the app
  is paused; a running clock made a 3-minute lock fire the SDK's drive timeout alarm ("An error has occurred").
- Touch layout (touch_android.c, SR_IOS): unit = min(screen height, 340 pt iPhone / 390 pt iPad), bottom controls
  lifted 0.04 H + 0.2 (H - unit), right cluster 0.07 unit in, START 0.5 unit from the right (Control Center).
- devicectl `device capture screenshot` returned black frames on the iPad Pro (iPadOS 26.6); use the game's own
  captures (SR_SCREENSHOT) and the user's eyes for system alerts.

## Mac launcher (2026-11-09)

- `launcher/mac/SonicRidersLauncher.swift` (SwiftUI, one file) -> `Sonic Riders Launcher.app` by
  `tools/build_mac_launcher.py` (xcrun swiftc, arm64, macOS 13+; icon = the Super Sonic art of every launcher,
  launcher/icon_256.png + icon_32.png -> AppIcon.icns via sips + iconutil, 16-256 px; gold-ring fallback; banner.jpg in Resources). Signed ad hoc **after** assembling (`codesign --force --deep -s -`): a bundle
  whose files don't match its signature is "damaged" to Gatekeeper with no Open Anyway. make_release.py --flavor ios
  builds it (on a Mac only; refuses if its sources differ from REF) and ships it with "START HERE (Mac).html"
  (launcher/mac/START_HERE.html) next to the .command (kept as the Terminal route).
- Pages: Get ready (folder without spaces, else "Move it to ~/SonicRiders" + relaunch; Apple silicon; Xcode -> App
  Store; first launch; iOS platform `xcodebuild -downloadPlatform iOS`; Metal Toolchain; Homebrew via a Terminal
  .command running brew.sh's installer; ninja + shaderc `brew install`; Apple ID optional), Build (disc, icon
  picker with the real converted preview, signed / unsigned; runs `setup_mac.py --yes --install no --iso --icon
  --ipa-dir [--unsigned]`, progress from `== N/7` and ninja `[a/b]` lines), Install (Sideloadly in 7 steps, or
  from the Mac via ios_run.py install / push-iso), Renew (install date in UserDefaults
  `net.srnative.sonicriders.launcher`, expiry, .ics Calendar reminder 6 days on), About.
- Root: the nearest folder above the app with tools/setup_mac.py; a quarantined app runs translocated, so
  `SecTranslocateCreateOriginalPathForURL` (dlsym) gives the real path. At start it removes the quarantine flag
  from the whole folder (nothing else asks Gatekeeper).
- Test variables: `SR_LAUNCHER_ROOT`, `SR_LAUNCHER_PAGE=0-4`, `SR_LAUNCHER_SHOT=<png>` (off-screen picture, quits),
  `SR_LAUNCHER_BUILD=1` (builds at once; with SHOT the picture is taken when the build ends), `SR_LAUNCHER_ISO`,
  `SR_LAUNCHER_IPA_DIR` (never the Desktop folder in tests).
- Gatekeeper on macOS 27.2 (strings read from CoreServicesUIAgent Quarantine*.loctable and SecurityPrivacyExtension
  Localizable.loctable, not from screenshots: this session had no screen-recording permission): first open ->
  "“X” Not Opened" / "Apple could not verify “X” is free of malware..." (X = "Sonic Riders Launcher.app"); the blue default button on
  top is Move to Trash, Done is the grey one below; the icon is the lock dial, not the app's (user's screenshot); then System
  Settings > Privacy & Security > Security: "“X” was blocked to protect your Mac." + Open Anyway. A downloaded
  *script* (.command) gets "File Not Opened" ("Scammers often encourage ... running scripts"), which is why the
  .app is the main route. Full sequence confirmed from the user's screenshots (2026-11-09):
  Settings row adds a grey caption "Apple could not verify ..."; Open Anyway -> alert "Open “X”?" ("Apple is not
  able to verify ...") with Move to Trash (blue, top) / Open Anyway / Done -> Touch ID or "Use Password..." ("You
  are attempting to open an app that may cause harm ...") -> the launcher starts and macOS asks for Downloads and
  Desktop folder access (TCC; Allow).
- Zip modes: v0.7.1's zip already stored 0755 for the .command (Unix origin); a cold Archive Utility extract with
  and without a quarantine flag kept 0755, so the missing execute bit the user met was not reproduced. Entries now
  also carry S_IFREG (as Info-ZIP writes) [likely harmless either way].
