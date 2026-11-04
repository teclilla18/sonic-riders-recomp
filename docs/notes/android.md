# Android port

**Reference.** Moved from docs/REFERENCE.md on 2026-11-03 (content unchanged). Index: [REFERENCE.md](../REFERENCE.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## Android: build, install, test (branch `android`, 2026-11-03)

- Plan and milestones: docs/ANDROID_PLAN.md. Tools (setup `-Target android -AcceptAndroidSdkLicense`, pins in
  `setup/toolchain.json` "android"): `<tools>/android-sdk/{ndk/27.3.13750724, build-tools/35.0.0,
  platforms/android-35, platform-tools}`, `<tools>/jdk17`, `<tools>/sdl2-src`; signing key
  `<tools>/android/sonicriders-local.keystore` (keep it: another key cannot update the installed app, and
  uninstalling deletes the app folder).
- `python configure.py --target android --android-sdk <tools>/android-sdk --jdk <tools>/jdk17 --sdl-src
  <tools>/sdl2-src --abi arm64-v8a|x86_64 --keystore <key>`, then `ninja` → `build/android/sonicriders-<abi>.apk`
  (objects in `build/android-<abi>/`, unstripped `libmain.so` there; the APK gets stripped copies, ~26 MB).
  `--android-app probe [--abi both]` builds the milestone 1 probe (`tools/android/probe/`) instead. build.ninja holds one target at a time: reconfigure before
  building host/switch again. Steps (no Gradle): `tools/android_build.py` (ndk-build SDL2 + libc++_shared,
  javac + d8, aapt2, zipalign -P 16, apksigner); `.so` stored uncompressed, 16 KB segments.
- Java: `android/java/net/srnative/sonicriders/SonicRidersActivity.java` (SDLActivity; intent extra `args`:
  space-separated words to argv, KEY=VALUE become environment variables). Manifest template
  `android/AndroidManifest.xml.in` (minSdk 30, targetSdk 35, sensorLandscape, profileable).
- Test run: `python tools/android_run.py --apk <apk> --package <pkg> --wait <regex> --pull <file> [--args ".."]
  [--serial S]`: install, start, logcat of the app's pid only, files from `/sdcard/Android/data/<pkg>/files`,
  results in `build/android/run/`. Packages: game `net.srnative.sonicriders`, probe `net.srnative.sonicriders.probe`.
- Game on Android (milestone 2): platform = `platform/host` (POSIX branch of platform_host.c, audio_sdl.c, input_sdl.c)
  + `platform/android/render_android.c` (frames recycled, SDL events pumped; no picture until milestone 3). The app
  folder `/sdcard/Android/data/net.srnative.sonicriders/files` is the working directory: `baserom/*.iso`, `saves/`,
  `env.txt`, `sonicriders.log` (also logcat tag `sonicriders`), `build/` for `SR_RAMDUMPAT` dumps (make it first).
  Disc image: `adb push <iso> <folder>/baserom/SonicRiders.iso` (35 s over USB 3; works on Android 17). From Git Bash
  set `MSYS_NO_PATHCONV=1` or device paths get rewritten. Renderer: `platform/android/render_vk.c` (SPIR-V from
  `tools/embed_shaders.py spv`, prelude PRELUDE_VK: set 0 = UBOs binding 0/1 with dynamic offsets, set 1 = 8 textures;
  `DEPTH_ZERO_ONE` remaps clip z in gx.vert; Vulkan-only quads `runtime/shaders/vk_*.{vert,frag}`). Touch:
  `platform/android/touch_android.c` (layout in units of the screen height). Self-captures: `SR_SHOTS=a,b` ->
  `shot_<n>.bmp` in the app folder (the screen, with menu and touch controls). The app must create its folder itself (start it once): a
  folder made from the adb shell first is owned by `shell` and the app cannot read the image in it (seen on the emulator).
  Deterministic compare without a route: `SR_PERFTEST=1 SR_DETCLOCK=1 SR_FRAMERATE=0 SR_RAMHASH=N` and the same
  `saves/settings.ini` on both machines (widescreen and the Chaos Emerald option change game memory). x28 base register not used yet (generic memory base load). Vulkan prelude `CLIP_Y 1.0` (not deko3d's -1).
- Phone vs PC picture compare: on both run `SR_PERFTEST=1 SR_DETCLOCK=1 SR_FRAMERATE=0 SR_SHOTSTEP=<game frames>` (phone
  also `SR_TOUCH=0`) with the same `saves/settings.ini`; PC writes `build/shot_<n>.png` (window size), the phone
  `shot_<n>.bmp` (screen size; n can differ by one presented frame). Crop both to the picture rectangle of the aspect
  and resize before diffing. Vulkan files in the app folder: `vk_pipeline_cache.bin` (driver cache) and
  `vk_pipeline_keys.bin` (magic "VPK1", count, u64 keys; prebuilt at boot by a helper thread). Log: `[vk] ... ms/frame
  on the render thread` every 300 frames, then `parts` (copy-in, recording, acquire, submit, present) and `GPU`
  (timestamps) lines; a slow present means the GPU is behind.
- Touch pictures `android/touch_icons/*.png` are tracked; the guard allows exactly those paths with the SHA-1 in
  `tools/check_no_game_data.py` `OWN_ART` (update the hash when a picture changes).
- Emulator (dev only, not in the setup): emulator 16740729 + `system-images;android-35;google_apis;x86_64` r09
  unpacked into `<tools>/android-sdk` (Google SHA-1 checked), AVD `srtest` in `<tools>/avd` (`ANDROID_AVD_HOME`),
  started with `-no-window -gpu host` (WHPX). Its GLES stops at 3.1 and its GPU is the PC's: no performance numbers.
- Touch controls (2026-11-03, second round): stick home a little north-east (`stick_home`); dead zone 12% in races,
  35% with a squared start in the game's menus (`stick_value`, by `GAME_TOP_MODE`; the log says `[touch] game mode N`
  when it changes); PAUSE is a circle with START drawn in vk_touch.frag (radius 0.065 H). Menu by touch: Close and
  Quit inside the ImGui frame now trigger a redraw (`overlay_image`, `open_before`); no Fullscreen on Android;
  Controls tab text for touch + pads.
- App icon: `tools/android_icon.py` makes an adaptive icon (foreground 432 px with the picture in the middle 288 px,
  background of its edge colour) at APK build time from `baserom/_Main.rel` (the save icon without its 2-px frame,
  whole-pixel scaled), or from `configure.py --icon <.ico|.png>` / setup `-Icon` (also used for the PC .exe icon).
  aapt2 compiles it (`res.zip`) and the manifest gets `android:icon` through `@ICON@`.
- Triggers (PC and Android, input_sdl.c `trigger`): per pad, the deepest press is learned (start 26000 of 32767);
  analog = value scaled to that peak; digital L/R click from 85% of the peak (analog 255 while clicked). Log:
  `[input] port N left/right trigger: pressed to V of 32767 (peak P, click from C)` for each new deepest press.
- S23 Ultra facts (Android 17, SM8550): CPUs 0-2 A510 2016 MHz (capacity 266), 3-6 2803 MHz (811), 7 X3 3360 MHz
  (1024); affinity and nice -16/-19 allowed for apps; APerformanceHint, AThermal, ANativeWindow_setFrameRate
  present; SDL audio uses OpenSL ES at 32 kHz, 512 frames; display 3088x1440 at 120 Hz (frames of 8.3 ms).
