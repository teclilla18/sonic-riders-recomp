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

