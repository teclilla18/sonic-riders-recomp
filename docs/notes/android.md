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
- Crash reports (2026-11-05): `crash_signal` in platform_host.c (SIGSEGV/BUS/ILL/FPE/ABRT, through ART's
  sigchain) writes `crash.txt` in the app folder with plain `write()` (signal, fault address for real faults, pc,
  lr, `_Unwind_Backtrace` as `lib+offset symbol`), then logcat and the log, then the guest dumps; then the old
  handler runs (debuggerd still writes its tombstone; sent signals are re-raised). On the next launch,
  `CrashReport.collect` (before `super.onCreate`, so before the log is rotated) reads the newest
  `ApplicationExitInfo` (CRASH, CRASH_NATIVE, ANR, INITIALIZATION_FAILURE, EXIT_SELF with status != 0; newest
  timestamp kept in prefs `crashreport/exit_seen`) or a `crash.txt`, and writes header + crash.txt + log (first
  256 KB + last 768 KB) to `crash-report.txt` and via MediaStore to `Download/SonicRiders/sonicriders-crash-<time>.txt`,
  then a dialog with Share report (ACTION_SEND of the MediaStore Uri). Summary line: last `FATAL:` of the log, else
  crash.txt's first line. Java exceptions also go to crash.txt. Test on the emulator: launch args `missing.iso`
  (rt_fatal -> abort) or `adb root` + `adb shell kill -SEGV <pid>`, then start it again. A missing disc image
  (`cannot open the disc image`) shows up this way, as a "crash on launch".
- Touch controls (2026-11-03, second round): stick home a little north-east (`stick_home`); dead zone 12% in races,
  35% with a squared start in the game's menus (`stick_value`, by `GAME_TOP_MODE`; the log says `[touch] game mode N`
  when it changes); PAUSE is a circle with START drawn in vk_touch.frag (radius 0.065 H). Menu by touch: Close and
  Quit inside the ImGui frame now trigger a redraw (`overlay_image`, `open_before`); no Fullscreen on Android;
  Controls tab text for touch + pads.
- App icon: `tools/android_icon.py` makes an adaptive icon (foreground 432 px with the picture in the middle 288 px,
  background of its edge colour) at APK build time from `setup/icon.png` (the fan icon by cedry2kio; since 2026-11-11 the save icon is never
  used as a default), or from `configure.py --icon <.ico|.png>` / setup `-Icon` (also used for the PC .exe icon).
  aapt2 compiles it (`res.zip`) and the manifest gets `android:icon` through `@ICON@`.
- Triggers (PC and Android, input_sdl.c `trigger`): per pad, the deepest press is learned (start 26000 of 32767);
  analog = value scaled to that peak; digital L/R click from 85% of the peak (analog 255 while clicked). Log:
  `[input] port N left/right trigger: pressed to V of 32767 (peak P, click from C)` for each new deepest press.
- S23 Ultra facts (Android 17, SM8550): CPUs 0-2 A510 2016 MHz (capacity 266), 3-6 2803 MHz (811), 7 X3 3360 MHz
  (1024); affinity and nice -16/-19 allowed for apps; APerformanceHint, AThermal, ANativeWindow_setFrameRate
  present; SDL audio uses OpenSL ES at 32 kHz, 512 frames; display 3088x1440 at 120 Hz (frames of 8.3 ms).

## Specialized fragment shaders (2026-11-03)

- As on the Switch: `tools/gen_shaders.py --vk` compiles one SPIR-V fragment shader per recorded state key (TEV
  setup folded into constants) with the NDK's glslc into `build/android-<abi>/gen/gx_spec_spv.h`; keys come from
  `config/GXEE8P/shader_keys.txt` (tracked since v0.5.2) plus local extras in `build/shaderkeys/*.txt`.
- render_vk.c: `spec_bits()` per state change (binary search by key hash) → pipeline key bits 36-59 (low 24 bits
  of the hash) + bit 60; `gx_pipe_make` picks the module. A key from an older table that no longer matches keeps the
  ubershader. Misses go to `shader_keys.txt` in the app folder: pull it (`--pull shader_keys.txt`) into
  `build/shaderkeys/android_<date>.txt`, rebuild, and merge new keys into `config/GXEE8P/shader_keys.txt`. `SR_NOSPEC=1`: ubershader only (A/B).
- Log: `[vk] ... specialized shaders for N of M states` every 300 frames.
- A/B on the S23 Ultra (Android 17), same build, `SR_PERFTEST=1 SR_DETCLOCK=1 SR_FRAMERATE=0`: 1P demo 55.1 → 59.3
  fps, 2P demo 58.4 → 59.8 fps; GPU per frame: title 9-11 → 1.6-1.8 ms, 1P race 13.7-18.2 → 11.9-13.2 ms (first
  heavy frame 23.7 → 18.6), 2P race 13.0-17.7 → 12.0-13.9 ms. All demo states hit (32 keys from the Switch files).
- Tried and dropped (2026-11-03): a packed Vulkan vertex (116 instead of 160 bytes: unorm8 colours, half-float
  matrix indices, normal without w; Adreno has no uscaled8 vertex format). Same APK A/B: no change in fps or GPU
  time (race frames 12-14 ms either way), identical pictures. Vertex fetch is not the cost; reverted.
- Remaining GPU leads (untested): per-draw cost on the tiler (4 EFB passes per race frame: each copy ends a render
  pass, so tiles are stored and reloaded), the vertex shader's XF reads, render resolution. Needs a GPU profiler
  (Android GPU Inspector / Snapdragon Profiler) before more guessing.

## Open: device lost while screen recording (2026-11-04) [confirmed repro, cause unknown]

- Symptom: `FATAL: [vk] vkQueueSubmit(...) failed: -4` (VK_ERROR_DEVICE_LOST), Adreno `log_gpu_snapshot` just
  before. The user hit it while recording and playing (warm phone); reproduced 4 of 4 with the perf test plus
  `adb shell screenrecord`, and once without recording on the warm phone (older build).
- Pattern: from the first heavy race frame (~6,000 draws, right after "Now Loading") every frame takes ~330 ms
  (`present` or `submit` blocks), then the device is lost after 4-5 such frames. Without recording that first
  frame alone costs 85-99 ms of GPU (every build since 2026-11-03 17:00, ubershader too), then normal.
- Ruled out, each still crashing (patch kept locally, not committed): EFB copy/clear rectangles clamped (none were
  out of bounds); frames split into submissions of 1,500 draws; robustBufferAccess on; the swapchain acquire waited
  at colour output instead of ALL_COMMANDS; `SR_VK_TINYSCISSOR=1` (1-pixel scissor) still crashes, so not pixel cost.
  The driver offers no VK_EXT_device_fault.
- Next ideas: recover from device loss (rebuild the Vulkan state, re-upload textures); present at a lower surface
  size (Java `SurfaceHolder.setFixedSize`) and pre-rotate (swapchain transform 1 vs current 2), so the compositor
  and recorder do less; fewer swapchain images (now 5).
- Released in v0.4.0 with this as a known issue (user's decision).
- 2026-11-05, v0.4.2, a normal play session while recording (via the new crash report): same pattern at 2,769
  draws/frame (not ~6,000), `present` 297 ms per frame for 4 frames, then device lost. One recording session
  before it did not crash: intermittent.
- **2026-11-06: device-loss recovery** (render_vk.c `device_recover`): on VK_ERROR_DEVICE_LOST (submit, present,
  fence waits, acquire) the frame is dropped; the next frame tears down everything on the VkDevice (the instance
  and surface stay), makes it again (`device_setup`), reloads the pipeline cache file, rebuilds this run's
  pipelines (prewarm, waited for) and re-uploads textures from their pixels (EFB-copy textures come back with the
  game's next copy). Log: `[vk] device lost, recovered (N): device ~250 ms, pipelines ~110 ms`. A 4th loss within
  10 s ends with `rt_fatal` (crash report as before). Tested: `SR_VK_FAKELOST` in the intro and in the 1P race,
  pictures right 2 frames after [confirmed, S23 Ultra]; real losses: 3 rebuilds each worked [confirmed].
- **The screenrecord repro still ends in the fatal error** (rebuilt device lost again ~10 frames later, 4 of 4
  runs today). Bisect with recording (`build/old/rec_repro.sh`, ~90 s per run): still lost with fewer swapchain
  images (3), pre-rotation (images 1440x3088, transform = current), queue priority low or high
  (VK_EXT_global_priority), frames in parts of 300 draws with the GPU idle between parts, 30 fps mode, no dual-source
  blending, no push descriptors, fixed uniform offsets, the ubershader only, at most 20 pipeline switches per
  frame, 3 indices per draw (GPU frame then 32 ms), depth write as dynamic state, depth formats D32 / D32S8, an
  empty opposite-compare draw at each pass start (LRZ-off trick). **Survived**: no draws at all; ≤1,000 draws per
  frame (but many 340 ms presents); one pipeline per pass (`SR_VK_ONEPIPE`); depth writes forced on
  (`SR_VK_FORCE=dwrite`, also `depth`), but that picture is badly wrong (the sky dome hides the track) and its GPU
  frame drops to 7.6 ms, so "depth-write changes are the trigger" is [uncertain]. A race frame: ~6,200 draws, ~111
  pipeline switches, all blended, no lines/points/indirect; ~800 draws with depth test on and write off.
- Timing: the slow frames' own GPU time is ~12 ms (timestamps); the ~330 ms is spent in `vkQueuePresentKHR` (or
  submit), fence waits 0. The driver writes no snapshot (`panel.gpuSnapshotPath is not set`); full logcat shows
  nothing from SurfaceFlinger or the codec. Experiments patch (local):
  `build/old/android-devicelost-experiments-2026-11-06.patch`.
- Next ideas: an Adreno GPU profiler / snapshot (needs root or Snapdragon Profiler); test another Adreno phone or
  recorder; try a Vulkan render pass object instead of dynamic rendering; recording via the phone's own recorder
  (MediaProjection) to see whether it differs from `screenrecord`.

## Thread placement and ADPF hint (#14, 2026-11-07, on main)
- `rt_thread_pin` (platform_host.c, Android): every pinned runtime thread goes to the cores above the slowest
  max-frequency tier (cpufreq `cpuinfo_max_freq`; S23 Ultra: 3-7) with nice game -10, audio mixer -16, render / GP /
  devices -4, watchdog 0. Log: `[android] N cores, fast ones: ...`. `SR_NOPIN=1` off.
- ADPF: `platform_game_step_work` creates an APerformanceHint session for the game thread (dlsym, Android 13+;
  target 16.7 ms) and reports each step's work from hle_frame_end. Log: `[android] performance hint session ...`.
  `SR_NOHINT=1` off. S23 A/B (both features): no difference; the S23 never stalls.
