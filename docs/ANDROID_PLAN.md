# Android port: plan (2026-11-03, branch `android`)

Goal: one setup run on the user's PC, with their own Sonic Riders ISO, builds an APK for their own phone with
everything the Switch and Windows builds have. The APK holds recompiled game code, so like the `.nro` it is built
locally and never distributed (`*.apk`, `*.aab`, `*.so`, `*.dex`, `*.spv`, keystores: `.gitignore` and
`tools/check_no_game_data.py`). Status: **milestones 1 and 2 done (2026-11-03)**; next: milestone 3 (Vulkan renderer). Test device: Galaxy S23 Ultra (SM8550, Adreno 740),
Android 17, over wired adb. Marks: [confirmed] read in the code, [likely] strong reason, [uncertain] to be measured.

## 1. Inventory: what Android must match (from the code)

| Area | Switch | Windows | Where |
|---|---|---|---|
| Game | whole game, FMV, story, 1-4P races, saves (`card_a.raw`), DVD model | same | shared runtime |
| Sound | native AX mixed on a worker thread, 32 kHz | same, SDL queue | `ax.c`, `dsp.c`, `audio_*` |
| Settings menu | ImGui drawn in software to the overlay image; hold Minus 0.5 s | F1 / hold Back | `menu.cpp`, `menu_raster.cpp` |
| Display | 4:3 / 16:9 (patched cameras) / stretch; FPS counter; internal res 1x/1.5x/2x; 384 MHz handheld GPU | 4:3/16:9/stretch, FPS, fullscreen, fixed 2x | `settings.h` |
| Frame rate | 60 / 30 / Auto (full speed when a frame is late) | same | `frame_pacing.c` |
| Gameplay | Chaos Emerald unlock | same | `game_mods.c` |
| Mods | `mods/` file replacement + `mods.ini`, `[gear.*]`/`[memory]`/`[gecko]`, menu tab | same | `mods.c`, `mods_data.c` |
| Input | 4 players, Joy-Con sideways, system controller screen, rumble | keyboard + 4 SDL pads, rumble, 25% round dead zone | `input_hid.c`, `input_sdl.c` |
| Online | `SR_NET` lockstep, `SR_RECORD`/`SR_REPLAY`, own view `SR_VIEW`, online save (paused) | same | `netplay.c` (POSIX sockets) |
| Tests | `SR_PERFTEST`, `SR_DETCLOCK`, `SR_RAMHASH*`, `SR_INPUT`, `SR_SCREENSHOT`/`SR_SHOTS`, ~70 `SR_*` via `env.txt` | same via environment | REFERENCE "Debug variables" |
| Graphics extras | specialized shaders from recorded keys (AOT), GPU timestamps, own view | ubershader, key recording (`SR_SHADER_KEYS`), `SR_DFF` | `render_deko.c`, `render_gl.c` |
| Diagnostics | log + prev log, profiler thread, stall watchdog, applet sleep/wake | log + prev log, crash handler, sampling profiler | `platform_*`, `profile_win.c` |
| Install | `.nro` + forwarder NSP, icon | `.exe`, icon from the save icon, desktop shortcut | `setup.ps1`, `tools/nsp` |

## 2. Reuse map

Unchanged [confirmed]: recompiler and `build/gen/*.c`, CPU/scheduler/devices (`sched.c`, `vi.c`, `di.c`, `dsp.c`,
`ax.c`, `exi.c`, `si.c`), `gx_fifo.c`/`gx_render.c`/`gx_texture.c`, frame pacing, settings, ImGui menu (software
raster: no ImGui renderer needed), mods, netplay (POSIX branch), perf test, input script. `memory.c` already has an
`mmap` branch; the guest window is 16 KB aligned, which Android 15's 16 KB-page phones need.

Through SDL2 2.32.10 on Android (same version as PC, built from source with the NDK):

| Piece | SDL2 on Android | Android-specific work |
|---|---|---|
| Window/surface | `SDL_CreateWindow` + `SDL_Vulkan_CreateSurface` (or EGL/GLES) | swapchain recreate on resume, cutout/immersive |
| Audio | `audio_sdl.c` as is (AAudio, OpenSL ES fallback) | none at first; measure latency |
| Input | `input_sdl.c` as is (SDL_GameController, rumble, hot-plug) | touch overlay, Back-button menu, remap UI |
| Lifecycle | `SDL_APP_WILLENTERBACKGROUND` / `DIDENTERFOREGROUND` events | pause game/audio/clocks (as Switch sleep/wake), flush card |
| Files | `SDL_AndroidGetExternalStoragePath()` | `chdir` there: `baserom/`, `saves/`, `mods/`, `env.txt`, logs |
| Threads/timing | none | `platform_host.c` POSIX branch works; add affinity/priority (section 4) |

`platform_host.c` needs a small Android part (`#ifdef __ANDROID__`): `chdir` to the app folder, log to the file and
to logcat, `env.txt` and launch extras into `setenv` (as the Switch does), a `sigaction` crash handler (fault
address relative to `libmain.so` via `dladdr`), thread roles. `profile_win.c` is Windows only: the Switch profiler
(a thread sampling `g_ppc_cur_fn`) is portable and moves to shared code. Java: SDL's `SDLActivity` plus one small
subclass (launch extras as arguments, ISO picker). No other JNI. Arm64 code keeps the guest base in x28
(`-ffixed-x28`, as on Switch) [likely: x28 is callee-saved in bionic and the system libraries; checked in M2].

## 3. Graphics: reuse the Windows GL path, or Vulkan?

Option A, GLES 3.2 port of `render_gl.c`: ~150 changed lines (`#version 320 es` prelude, ES context, debug-only
`glGetTexImage` out). Gaps: dual-source blending (`index = 1` output) needs `GL_EXT_blend_func_extended`; no
internal-resolution setting and no specialized shaders in the GL path today; the driver compiles GLSL at run time
(stutter on first sight of each state); GL's per-draw CPU cost on phone drivers against 5,000-7,000 draws in busy
race views (hw33/hw36).

Option B, Vulkan `render_vk.c`: ~1,300 new lines, ported from `render_deko.c`, which is already an explicit API
backend (memory blocks, two frame slots with fences, descriptor tables, image pool, 1x/1.5x/2x, own view, GPU
timestamps all map almost 1:1). Shaders: the same GLSL with a Vulkan prelude (close to the deko3d one: explicit
bindings, `CLIP_Y -1`, `XF_UBO`), compiled at build time to SPIR-V with the NDK's `glslc`; `gen_shaders.py` gains a
SPIR-V output, so specialized shaders from `build/shaderkeys/` come too. New cost: pipeline objects (shader x
blend x depth x cull x mask x topology): dynamic state where present (core 1.1 viewport/scissor/bias; extended
dynamic state on 1.3 drivers), a `VkPipelineCache` saved in the app folder, and pipeline keys recorded and
prebuilt at boot like the shader keys.

**Decision: Vulkan (B)** [confirmed by the M1 measurement on the S23 Ultra, Android 17]: 7,000 small draws with a
uniform range each and a state change every 8 cost **8.06 ms** of CPU per frame in GLES 3.2 and **2.00 ms** in Vulkan
1.3 (4x), medians of 180 frames. The device also has `dualSrcBlend`, extended dynamic state 1 and 2 (not 3), push
descriptors, dynamic rendering and timeline semaphores; GLES has `GL_EXT_blend_func_extended` and framebuffer fetch.
The swapchain reports a 90-degree transform (landscape on a portrait panel): the backend must pre-rotate. Reasons:
draw-call cost is the known limit of GameCube rendering on phones; ahead-of-time SPIR-V plus a pipeline cache fits
the "built in advance" approach the Switch already uses; it starts from the faster backend (deko3d) instead of the
simpler one. If the user's GPU lacks Vulkan 1.1 or the measurement favours GLES, A becomes the fallback.

Mobile shader compilers: keep the uam rules for every backend (no runtime index into a vector or local array; use
`pick()`/`comp()`/`cinput()`/`ainput()`/`reg()`; no loop-carried vec4 accumulator seeded from a parameter, no
`continue` in vector loops), run `spirv-val` in the build, and check each GPU family against PC reference frames
(same frame number on the det route, pixel diff). Dual-source blending: Vulkan `dualSrcBlend` is missing on some
Mali drivers [uncertain]; fallback through framebuffer fetch (`VK_EXT_rasterization_order_attachment_access` /
`GL_EXT_shader_framebuffer_fetch`, cheap on tile GPUs). M1 probes all of these on the device.

## 4. Threads on big.LITTLE

Linux time-slices equal priorities, so the Horizon starvation problem does not exist; the risk is the opposite:
the energy-aware scheduler parks the game thread on a small core, or thermals cut the big cores.
- At start, read `/sys/devices/system/cpu/cpu*/cpu_capacity` (else `cpufreq/cpuinfo_max_freq`), group cores into
  clusters, log the topology.
- Replace "pin to core N" with roles (`rt_thread_role`; the Switch maps roles to today's cores, unchanged):
  game thread → mask of the fastest cluster(s) (prime + big); render thread → big cluster; AX mixer → big/mid with
  nice -16 (`THREAD_PRIORITY_AUDIO`; try -19 and log what was granted); timers, DVD, card writer → little/mid.
  Masks, never a single core (thermal migration, OEM cpusets); if `sched_setaffinity` fails, log it and go on.
- Android Dynamic Performance (`APerformanceHint`, API 33+) for the game and render threads with a 16.7 ms target
  (33.3 ms in 30 fps mode), reporting each frame's work: clocks stay up without spinning. Thermal headroom
  (`AThermal`) feeds Auto mode and a warning.
- The guest's idle loops must sleep, never spin (battery, heat): check the `PROF_IDLE` share on the phone.
- Measure, don't guess: per-thread busy %, `sched_getcpu()` samples and frame times in the log; `simpleperf` for
  hot spots (the APK is built `profileable`).

## 5. Setup flow

`setup.ps1 -Target switch|pc|android|both|all` (`both` stays Switch + PC) with `-Abi arm64-v8a|x86_64|both`
(default arm64-v8a), `-Install yes|no` (adb install if a phone is connected), `-PushIso yes|no`, and
`-AcceptAndroidSdkLicense` (`-Yes` alone never accepts Google's license; the setup shows it otherwise).
`configure.py --target android --android-sdk DIR --ndk DIR --jdk DIR --abi A` writes the same kind of ninja file.

Tools, pinned by SHA-256 in `setup/toolchain.json` as today, in `<ToolsDir>`: a Temurin JDK 17 zip and Google's
command-line tools zip; then `sdkmanager` (which checks its own package checksums) installs exact versions of
`platform-tools`, `platforms;android-35`, `build-tools;35.0.0` and one NDK (r27 LTS line, exact version in M1); the
SDL2 2.32.10 source zip. No admin rights, nothing installed system-wide. About 2.5 GB of downloads once.

APK without Gradle (Gradle fetches hundreds of MB from Maven at build time and is hard to pin): ninja runs NDK
clang (`aarch64-linux-android29-clang`, `-fPIC -fvisibility=hidden`, `-Wl,-z,max-page-size=16384`) → `libmain.so`;
`ndk-build` → `libSDL2.so`; `glslc` → SPIR-V embedded; `javac` + `d8` → `classes.dex` (SDL's Java + our activity);
`aapt2` → manifest, resources, icon (`make_icon.py`'s save icon as PNG); `zipalign -P 16` (uncompressed, aligned
`.so`); `apksigner` with a keystore made once by `keytool` in `<ToolsDir>` (never in the repo).
Output `build/android/sonicriders-<abi>.apk`. minSdk 30 (Android 11: Vulkan 1.1, AAudio, C11 `<threads.h>` in bionic), targetSdk 35.
Trade-off: we must follow SDK changes by hand; if that bites, a Gradle project is the fallback.

Game data: **read at run time, not packed in the APK** (recommended).

| | ISO inside the APK (uncompressed asset) | ISO in the app folder (recommended) |
|---|---|---|
| APK size / reinstall | ~1.45 GB, every test install copies it again | ~30-60 MB, fast test loop |
| Storage | APK + install copy during install | one 1.4 GB copy |
| Distribution risk | the whole game in one shareable file | APK useless without the user's disc |
| Reading | `AAsset_openFileDescriptor64` + offset | plain file, as on PC and Switch |

The setup pushes the ISO once with `adb push` to `/sdcard/Android/data/<package>/files/baserom/`. Without USB
debugging, the first launch offers a file picker (Storage Access Framework): the SHA-1 is checked (GXEE8P rev 0),
then the ISO is read through its file descriptor if seekable, else copied into the app folder once.

## 6. Milestones (one per session; each ends with Switch + Windows still building)

1. **Toolchain + test APK.** `setup -Target android` fetches the tools; `configure --target android` builds a
   small SDL2 APK (no game code): clear colour, a tone, input events logged, a capability probe (Vulkan version,
   `dualSrcBlend`, extended dynamic state, framebuffer fetch, GLES extensions, core clusters) and the 7,000-draw
   GLES vs Vulkan measurement. Verify: installs and runs on the phone and the emulator; logcat lines; a screenshot
   pulled. Decides section 3.
2. **Game code boots, no picture.** `libmain.so` with the recompiled code, ISO from the app folder, renderer
   recycling frames (as `SR_HEADLESS`). Verify: logcat shows `[mem]`, `[vi]`, frame count rising; `SR_DETCLOCK` +
   `SR_RAMHASH` on the det route equals the Switch run (arm64) and the PC run (x86_64 emulator).
3. **Title screen.** `render_vk.c` (or GLES per M1), menu overlay. Verify: self-captures of the intro and title
   screen pulled with adb and diffed against PC captures of the same frames.
4. **Audio + lifecycle.** SDL audio, background/foreground pause and resume, card flush. Verify: sound by ear,
   "mixes started late" ~0 in the log, Home and back mid-race resumes cleanly.
5. **Controls and saves.** Bluetooth pads (4 ports, rumble), Back-held menu, create/load a save. Verify: a race
   played with a pad; save survives an app restart.
6. **Full setup flow.** One command from ISO to installed APK (both ABIs), ISO push or picker, perf test.
   Verify: clean `<ToolsDir>` run; `SR_PERFTEST` record in `perf/results/` (with device, Android version,
   temperature); CHANGELOG entry.
7. **Android extras**, one or two per session (section 7).

## 7. Android extras (after parity)

Asked for: touch controls overlay (drawn into the existing overlay image; layout and opacity in the menu; hidden
while a pad is connected); Bluetooth controller mapping (SDL's mapping database plus a remap tab, per-controller
profiles) and rumble (pad rumble; phone vibration with touch controls); resolution scale (1x/1.5x/2x/3x) and
performance presets (Battery / Balanced / Quality: resolution, frame rate mode, performance hint target); frame
pacing (`ANativeWindow_setFrameRate(60)` so 90/120 Hz screens run an even 60, Swappy-style present timing);
pausing cleanly in the background (game, audio and clocks stop, card flushed, swapchain released).
Suggested: save import/export through the file picker (the card image is the same file on PC, Switch and phone);
thermal warning and Auto downgrade; immersive mode and screen cutouts; screenshot to the gallery; handheld
Android devices (Retroid, AYN: pad-first menu); online play with PC/Switch once phase 4 lands (arm64 rounds NaNs
like the Switch).

## 8. Testing

- Builds: arm64-v8a for the phone, x86_64 for the emulator (an x86_64 system image, emulator with `-gpu host`
  for Vulkan). The emulator checks building, booting and logic; performance numbers only from the phone, always
  with device, Android version and battery/temperature state (as the Switch clocks rule).
- `tools/android_run.py` (new): `adb install -r`, `am start` with `SR_*` as launch extras (or `env.txt` pushed),
  `adb logcat` capture filtered to the app, pull `sonicriders.log`, `shot_*.bmp`, `perftest.json` from the app
  folder, and compare shots with PC captures of the same det-route frames. The port's own captures are the
  reference; `adb exec-out screencap` only for system UI problems (touch overlay placement, cutouts).
- Every milestone: `ninja` for host and switch still succeeds; guard + `git status --ignored` before commits.
