# Reference

Durable facts about the port: how it is built, how it is put together, how to test it, and what is known about the
game. Dated stories of how things were found are in [HISTORY.md](HISTORY.md); the current status is in
[../STATE.md](../STATE.md). Online play has its own design log in [ONLINE.md](ONLINE.md), the performance test in
[PERF.md](PERF.md), mods in [MODDING.md](MODDING.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

Don't read this file whole: `grep -n '^## ' docs/REFERENCE.md` lists the sections, then read the one you need.

Sections:
- Project goal
- Debug variables (SR_*): full list
- Recompiler switches (RECOMP_*)
- Known game addresses
- Shared GLSL rules (uam)
- Testing tools: Eden, Dolphin, test scripts
- Switch: build, install and test
- Host (Windows): build and run
- Setup tool, releases, PC icon
- GitHub and pushing
- Device and replacement boundary as implemented
- Debugging aids (original list)
- Plan: ahead-of-time specialized shader cache
- Architecture decisions (2026-10-15), SDK map of main.dol
- Phase 1 — Recon (2026-10-15)
- Phase 2 — matching-decomp tooling plan (superseded)
- Open questions (recon era), Naming decisions

## Debug variables (SR_*): full list

Set them in the environment on PC; on the Switch put `KEY=VALUE` lines in `env.txt` in the game's SD folder (lines up
to 8 KB). "PC" = desktop build only. Details: grep the variable in this file and in HISTORY.md.

**Running and capturing**
- `SR_MINIMIZED=1` (PC): window stays in the background. Use for every test run (the user may be using the PC).
- `SR_HEADLESS=1` (PC): no window. `SR_NOVSYNC=1` (PC): present without vsync.
- `SR_NOAUDIO=1`: silent host output. `SR_AUDIO=0`: mixer off. `SR_WAVOUT=<file>`: record the mixed output.
- `SR_SCREENSHOT=<frame>[,...]` / `list` (PC): `build/shot_<frame>.png`, up to 512 frames. `SR_SHOTSTEP`: capture by frame end (from 1).
- `SR_SHOTS=1` or `SR_SHOTS=a,b,c` (Switch): `shot_<frame>.bmp` at 300 … 7200, or those frames. Off by default.
- Android only: `SR_TOUCH=0` hides the touch controls (picture compares); `SR_VK_TINYSCISSOR=1` (1-pixel scissor: GPU cost without pixels) and `SR_VK_FULLAREA=1` (whole-EFB render area) are GPU cost tests; `SR_VK_NOPUSH=1` no push descriptors.
- `SR_INPUT="1500:START,1510:-START,7300:2.START,4000:SX=50,..."`: scripted buttons/stick at VI field counts, port prefix `N.`; up to 1024 steps; PC and Switch (input_script.c).
- `SR_PADS=N`: connect ports 1..N without gamepads (PC). `SR_CARD=<path>`: memory card image (both targets).
- `SR_TEST_MENU=1` / `<tab>` / `mods`: start with the settings menu open on that tab.
- `SR_FRAMERATE=0/1/2`: 60 fps / 30 fps / Auto for tests. `SR_TEST_GAME_LOAD_MS=DRAW[:LOGIC]`: busy-wait on the game thread to imitate a slow console.
- `SR_HANDHELD_GPU=0` (Switch): don't apply the 384 MHz handheld GPU profile.

**Hangs, crashes, timing**
- `SR_WATCHDOG=<s>`: function trace + device accesses + guest threads dumped when time runs out. `SR_WATCHDOG_NOTRACE=1`: dump without the trace. On the Switch its exit ends in a system error screen.
- `SR_LOGSYNC=1`: flush every log line. `SR_DVDLOG=1`: every disc read by file name, completions, ARAM DMAs.
- `SR_DVD_ATEND=1`: old disc model (data only at completion; test). `SR_DVD_MBPS=<n>`: modelled disc speed.
- `SR_WATCH=ADDR,VALUE` / `ADDR,~VALUE` / `ADDR,*[,A2,A3,A4]` (PC): watch guest memory, sample host threads.
- `SR_PEEK`, `SR_POKE` (per present, one byte), `SR_POKEAT=<frame>:<addr>:<hex>[,...]` (deterministic), `SR_DUMPRAM` (PC).
- `SR_GPTL=<field>`: timeline of game idle / PE finish / tokens / retraces. `SR_GPSLOW=<factor>`: slower GP thread (imitates the Switch).
- `SR_GX_SYNC=1`: no GP thread (old path). `SR_DSP_SYNC=1`: AX mixing on the game thread (old path). `SR_DECODE_HELPER=0`: no vertex-decode helper thread.
- `SR_DECODECHECK=1`: run old and new vertex decoders and compare. `SR_NOTLUTSNAP=1`: no palette snapshot at issue time.
- `SR_AXLOG=1`: dump AX parameter blocks every 400 lists. `SR_ARAMDUMP=<file>`: dump ARAM after 3000 lists.

**Profiling and performance**
- `SR_PROFILE=1`: PC sampling profiler (`tools/profile_report.py`); Switch `[profile]` lines every 10 s ("last function entered": a caller's work after a call is charged to the callee). `SR_PROFILE_WINDOW=FROM,TO`, `SR_PROFILE_OUT=<path>` (PC).
- `SR_PERFTEST=1` (+ `SR_PERFTEST_OUT=<path>`): the standard performance test, see docs/PERF.md.
- `SR_GPU_AB=1` (Switch): GPU timestamps and shader counters, alternating XF paths per frame.

**Graphics debugging**
- `SR_GXDEBUG=<frame>` (+ `SR_GXDEBUG_ALL`): that frame's viewport/scissor/copies/draws.
- `SR_TEVDEBUG=<view>|cycle|cycle:a,b,...` (PC): debug views (1 lit colour 0, 2 stage-1 texture, 3 stage 0 only, 6 stage-1 coordinates, 7 old specular direction, 8 lit colour 1, 9 no fog, 10 fog amount, 11 fog inputs). `SR_DK_DEBUG_TEV=<view>`: build-time, bakes a view into the deko3d shaders (tools/embed_shaders.py).
- `SR_SKIPSAME=<frame>,<n>[,<first>]` (PC): redraw a frame without each block of n draws (`build/same_<frame>_<k>.png`). `SR_SKIPSCAN=<frame>,<step>` (PC): related scan mode in render_gl.c.
- `SR_DFF=<frame>[,<count>]`: record frames as a Dolphin FIFO log (record with `SR_GX_SYNC=1`).
- `SR_DUMPTEX`, `SR_TEXWATCH=<hex addr>`, `SR_GLTEXDUMP=<hex addr>` (PC): textures, palette slots, the bound GL texture.
- `SR_NOALPHABLEND`, `SR_SHOWALPHA` (PC): alpha investigations. `SR_NOSPEC=1`: ubershader only (no specialized shaders).
- `SR_SHADER_KEYS=<file>`: log new specialized-shader keys (Switch writes `shader_keys.txt` on cache misses).

**Determinism and online play** (details in docs/ONLINE.md)
- `SR_DETCLOCK=1`: deterministic mode. `SR_DETTRACE=1`: log every interrupt/disc command with frame and location.
- `SR_RAMHASH`, `SR_RAMHASH_BLOCKS=<from>-<to>|none`, `SR_RAMHASH_SUB=<block>`, `SR_RAMHASH_REF=<log>` (+ `SR_RAMHASH_REF_DUMPS`), `SR_RAMHASH_KEY=<addr>` (adds e.g. the scene frame 8021BB64), `SR_RAMDUMPAT`, `SR_RAMDUMPKEY[_AFTER]`.
- `SR_RECORD=<file>`, `SR_REPLAY=<file>`, `SR_NET=host:[<addr>:]<port>` / `join:<addr>:<port>`, `SR_NET_DELAY`, `SR_NET_TIMEOUT`, `SR_NET_TEST_DESYNC=<frame>`, `SR_ONLINE_SAVE=1`, `SR_VIEW=<1|2>`.

## Recompiler switches (RECOMP_*)

Environment variables read by `tools/recomp/gcrecomp.py` when generating code (all on by default unless noted):
- `RECOMP_CALLEE_SAVED=0`: don't keep callee-saved registers in locals across calls.
- `RECOMP_SINGLE=0`: no single-precision tracking (singleflow.py); `RECOMP_SINGLE_CHECK=1`: checking build.
- `RECOMP_STACKRAM=0`: r1/r13-based accesses keep the device test.
- `RECOMP_HOT=0`: don't emit `config/GXEE8P/hot_functions.txt` functions first.
- `RECOMP_SUMMARY=0`: no register summaries at calls; `RECOMP_SUMMARY_CHECK=1`: checking build.
- `RECOMP_FLOAT=1` (off by default) / `RECOMP_FLOAT_CHECK=1`: single-precision ops computed in float (no gain).
- `configure.py --no-base-reg`: Switch without the x28 guest-memory base register.
- After editing `config/GXEE8P/patches.txt`, run `python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen`
  if ninja says there is nothing to do [uncertain].

## Known game addresses

Guest addresses as the port sees them (REL linked at 0x81800000; original GameCube REL addresses + 0x01404300).
`fn_`/`f_` names are dtk's. Where they were found: grep the address in HISTORY.md.

| What | Address | Notes |
|---|---|---|
| `main` | 80007A48 | |
| Frame end | `fn_8000610C` | GXCopyDisp 800312F0 → GXDrawDone 80030464 → VISetNextFrameBuffer; draw sync token 0xB00B (GXSetDrawSync 800303B0); retrace-count loop 800061C8 |
| VIWaitForRetrace | 80017038 | hooked by frame_pacing.c |
| Game's 30 fps mode | 8021BB7C (mode, u32), 8021BB80 (toggle), 8021BBF4 (f32 step multiplier = mode+1) | only setter `fn_1_4D310`; scenes use (mode+1) × 8021B8EC |
| Idle loop (SelectThread) | 8000D460 (OSEnableInterrupts) from 80010574 | shows as game-core idle in `[profile]` |
| DSPCheckMailToDSP / DSP handler | 80025E84 / `fn_8002612C` | |
| `__AI_SRC_INIT` | `fn_8001A5CC` | AISCNT 0xCC006C08 timing (boot stall fix) |
| DVDLowRead | `fn_80012408` | |
| memcpy / memset / memmove | 80003540 / 80003458 / 8003A354 | native in hle_misc.c |
| PSMTX/PSVEC routines | 8004C58C-8004D09C | e.g. 8004CF6C PSVECNormalize |
| Projection (widescreen) | `fn_800645DC`, REL text 0x14115C | patches.txt |
| r13 set / GQR clear | 80003334 / 8000A33C | |
| Player count | 8021B8C8 | stage suffix table REL rodata 0x2DA8+8 = "VMM" |
| Scene counter | 8021BB64 | resets per scene (perf test) |
| OS controller type | 80122014 | |
| Gear table | 0x819EA240 | 41 records × 0x1D0; +0x00 character mask, +0x10 top speed mod; level blocks 0x68/0x84/0xA0 (the older "+0x64" is off by 4); gear 18 = Chaos Emerald; speeds × 216 = km/h |
| Player array | 0x81A0D740 | 8 × 0x1080; +0x1034 gear id [uncertain, see Super Sonic notes], +0x1E4 position, level stats +0x8DC/+0x914/+0x94C |
| Camera | 0x81A05D74 | 0x38 per viewport; eye +0x14, target +0x20; view matrix 0x81A0583C; writer not found |
| Save data struct | 81AAD000 (0x6F0 bytes = card file offset 0x1460) | tracks +0x004..+0x013; +0x002 course flag [likely]; gear +0x348+i (bit 0 owned; Chaos Emerald byte 0x81AAD348+18); characters +0x3A0+c (== 1); time records +0x3BA [likely]; +0x6B8 ring total? [uncertain] |
| Gear selector / table | 81999080 / 819D55F8 | 54 × (u16 flag index, s16 character) |
| Super Sonic voice | `f_8188BE1C` | transformation = character Sonic+14 |
| CRI streaming loader | `f_819594F4` | loader thread 81A93B68, consumer 81A93E80 (prio 31), pump 81A93850, "available" counter 81A8FBAC |
| Loading-screen text table | REL bss 0x8CF14 | overwritten by the old IRQ stack bug |
| Speed-blur copy | phys 0x01106980 | 320×240 RGB565 sampled as 512×256 |
| Save icon | `_Main.rel` file 0x1F7660 (palette 0x1F7420) | tools/make_icon.py |
| Card file | `SONICRIDERS_GAMEDATA_01` | game data at file offset 0x1440-0x1B50 |

## Shared GLSL rules (uam)

The Switch compiles `runtime/shaders/` with uam, which miscompiles some patterns (found 2026-10-16 and 2026-10-29,
see HISTORY.md):
- Never index a vector or a local array with a runtime value. Use the explicit selects: `pick()` in
  `gx_common.glsl`, and `comp()`/`cinput()`/`ainput()`/`reg()` in `gx.frag`.
- In vertex shaders, avoid loop-carried vec4 accumulators seeded from a parameter, and `continue` in loops that
  accumulate vectors (`lights()` in gx.vert).
- After shader changes, every dksh in `build/switch/shaders/` must report `per_warp_scratch 0` (DkshProgramHeader at
  `programs_off`, offset 20).
- Specialized-shader keys must only include state the shader reads (gx_shader_key.c, gen_shaders.py): a stray bit
  makes every state miss and fall back to the ubershader (the 2026-10-26 slowdown). Check the `[deko3d]`
  "specialized shaders for N of M states" line in hardware logs.

## Testing tools: Eden, Dolphin, test scripts

**Eden** (Switch emulator): the original notes follow below ("Testing without hardware"). Also: start it with
`-WindowStyle Hidden`, never minimized (zero-size surface: divide-by-zero crash / rendering stalls); it is muted
(`user/config/sdl2-config.ini`); SR_INPUT now works there too through env.txt; GPU waits at new scenes are Eden
compiling shaders, and its numbers (JIT) are indicative only.

**Dolphin** (reference for how the real game looks):
- Dolphin 2609a, official build, portable in `build/ext/dolphin` (gitignored).
- `Dolphin.exe -b -e <iso>` with no input, unthrottled, frame dump (AVI; ffmpeg from the imageio-ffmpeg package
  reads it even unfinalized): the attract-mode demo races are the reference (Metal City race at Dolphin frames
  ~8800-12300). Runs are not frame-deterministic: compare by averages or the race timer.
- Hand-made `.dtm` input movies are ignored. For menus, `tools/dolphin_dsu_input.py` is a DSU (cemuhook) server that
  Dolphin reads as a controller (device `DSUClient/0/Script`; Start = Options, A = Cross; face buttons need the
  analog bytes). Player Entry with Sonic at Dolphin frame ~2590 from an empty card.
- `SR_DFF` frames play in Dolphin with `Dolphin.exe -b -e frame.dff`; needs `Core.RAMOverrideEnable=True`,
  `Core.MEM1Size=29360128` (our REL sits above 24 MB).
- `GFX.Settings.DumpTextures` via `-C` dumped nothing.

**Hardware over Wi-Fi:** the switch-remote MCP (buttons, screenshots, FTP, nxlink). The game's log is locked while
it runs (FTP read fails), and a read held open at launch made the game skip its log and env.txt. HOME tiles wrap; the
game tile asks "Who is using the software?" first; presses can be dropped, so check with a screenshot.
Console-specific details are in NOTES.local.md.

**Test routes and scripts (scratchpad, not committed; recreate if missing):** from the 2026-10-19 STATE notes:

**Test routes (scratchpad, recreate if missing):** route1p.txt (race), route2p/route4p.txt (Free Race 2/4 players, SR_PADS), routestory.txt / routestory2.txt (story, A presses through cutscenes), routestory3.txt (story, START skips); mp.ps1 -Keys -Shots -Secs -Pads [-Watchdog] (sets SR_NOAUDIO=1, SR_MINIMIZED=1); loop4p.ps1; cmpadx.py + adx.py (ADX decode and correlation against a WAV).

**Dolphin reference status:** installed and works for attract-mode demos (no input needed). Input movies (-m) do NOT take effect in 2609a here; if menu comparisons are needed, try a DSU (cemuhook UDP) fake controller with BackgroundInput.

Useful PC routes: the user's save route (START 1500/1800/2100, A 2450/2700/3050/3400; menu input from ~field 3930);
Player Entry (START at 300/600/900/1200, A at 1500..3300 every 250-300 fields); the 2P online route (mk2p.py, see
docs/ONLINE.md phase 3).

## Project goal

**Goal (changed 2026-10-15):** play Sonic Riders natively on a modded Switch via **static recompilation**, as SwitchWakerHD does for Wind Waker HD:
- PowerPC code is translated to C ahead of time.
- The Dolphin SDK is reimplemented natively.
- Graphics are drawn with deko3d.

A matching decomp is **not** the goal. The Phase 2/3 decomp-toolkit matching plan below is superseded.

## Switch: build, install and test

Older manual steps (the setup tool and `configure.py --target switch --devkitpro <tools>\devkitpro` are the current way, see CLAUDE.md and BUILDING.md):

**Switch: build, install and test**
- Build:
  ```
  $env:DEVKITPRO="C:/devkitPro"
  python configure.py --target switch
  ninja
  ```
  The result is `build/switch/sonicriders.nro`. It contains recompiled game code: never share or commit it.
- SD card layout (`sdmc:/switch/sonicriders/`):
  - `sonicriders.nro`
  - a **copy** of the user's disc image (any `*.iso`, GXEE8P rev 0)
  - created at runtime: `saves/card_a.raw` (memory card) and `sonicriders.log`
- Launch from the Homebrew Menu **in title mode** (hold R while starting any installed game). The guest memory window prefers `svcMapProcessCodeMemory`; without it, it falls back to `svcMapMemory`, and the log's `[mem]` line says which was used. Applet mode is untested and has much less memory.
- Controls: A B X Y = A B X Y, + = START, ZL/ZR = L/R, R = Z, sticks = main/C, D-pad. HOME exits. Hold − (Minus) for the settings menu.
- The log's `[perf]` lines give fps every 120 frames, plus a ms/frame breakdown. `[deko3d]` lines give render-thread and GPU ms/frame. `shot_<frame>.bmp` files (frames 300 … 7200) are self-captures. `env.txt` sets debug variables.

## Host (Windows): build and run

Original clang + MSVC method (the setup's llvm-mingw build is the current way, see CLAUDE.md):

**How to build/run (host, Windows):**
```
build/tools/dtk.exe dol split config/GXEE8P/config.yml build/GXEE8P   # once (analysis; writes config/GXEE8P/*symbols.txt)
python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen        # recompile (also run by ninja)
. tools/host/env.ps1                                                   # PowerShell: clang + MSVC/SDK paths (vcvars64.bat fails on this PATH)
python configure.py ; ninja                                            # ninja: %APPDATA%\Python\Python312\Scripts\ninja.exe
build/host/sonicriders.exe "baserom/Sonic Riders (USA) (En,Ja,Fr,De,Es,It).iso"
```
After a fresh clone, run `git config core.hooksPath tools/git-hooks` (the game-data guard).

## Setup tool, releases, PC icon

**Setup tool and releases (2026-10-22):** `SonicRidersSetup.bat` → `setup/setup.ps1` builds everything from the user's disc image with a SHA-256-pinned toolchain (`setup/toolchain.json`; downloads through Windows' `curl.exe`, since devkitPro's server refuses .NET's web client even with the pacman user agent). PC builds from it use llvm-mingw (`configure.py --toolchain mingw`, output `build/host-mingw/`). `tools/make_release.py --version X` zips the files tracked at HEAD (guard-checked) plus a `VERSION` file; the port logs `[build] <version or git describe>` first (`build/<target>/gen/version.c`, written by configure.py). Hardware performance so far is with an overclock (CPU 1785 / GPU 1152 / MEM 2133); stock clocks run poorly.

**PC program icon (2026-10-23):** [confirmed] `tools/make_icon.py` reads the memory card save icon (32x32 CI8, 8x4 tiles; RGB5A3 palette) from the verified `_Main.rel` at 0x1F7660 (palette at 0x1F7420; found by matching the icon in a real save in `saves/card_a.raw`), writes `build/icon.ico` (PNG entries 16/32/64/128/256: 2x2 average for 16, nearest 2x/4x/8x above) and `configure.py` compiles it in with llvm-mingw's windres when the file exists. SDL2 takes the window/taskbar icon from the program's first icon resource. The setup makes it after step 4 for the PC target (failure only warns). The disc banner (`opening.bnr`, 96x32 logo only) was too wide to read as a square icon.

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
- S23 Ultra facts (Android 17, SM8550): CPUs 0-2 A510 2016 MHz (capacity 266), 3-6 2803 MHz (811), 7 X3 3360 MHz
  (1024); affinity and nice -16/-19 allowed for apps; APerformanceHint, AThermal, ANativeWindow_setFrameRate
  present; SDL audio uses OpenSL ES at 32 kHz, 512 frames; display 3088x1440 at 120 Hz (frames of 8.3 ms).

## GitHub and pushing

**GitHub:** https://github.com/teclilla18/sonic-riders-recomp (public). Push at the end of every work session, after `python tools/check_no_game_data.py` passes.
- **Push at the end of every work session.**
- Before pushing, check `git status --ignored`: no ISO, DOL, REL, assets, disassembly, recompiled output or dtk symbol files.
- If anything questionable would be included, stop and ask the user.

## Device and replacement boundary as implemented

**Device and replacement boundary as implemented (revised from the plan below):**
- **Register-level device models**, which keep the game's own SDK code running: VI, DI (DVD), CP/PE/PI (GX FIFO), DSP/ARAM/AI, EXI (memory card + IPL SRAM/RTC), SI (controllers).
- **Renderer:** the guest side (`gx_render.c`, `gx_texture.c`) builds per-frame command lists. Backends: `platform/host/render_gl.c` (GL 3.3) and `platform/switch/render_deko.c` (deko3d). Shared ubershader GLSL in `runtime/shaders/`, embedded by `tools/embed_shaders.py` (uam for the Switch).
- **Memory card:** a new card is formatted natively exactly as the game's `__CARDFormatRegionAsync` does (serial = SRAM flash ID + CARD LCG from the format time). The IPL SRAM sets `flashIDCheckSum = ~sum(flashID) = 0xFF`, which `DoMount` checks for an already-unlocked card. The card reports "unlocked", so the DSP card-unlock task is never needed.
- **Native at function level**, only where the hardware semantics can't run natively:
  - `OSSaveContext`/`OSLoadContext`, i.e. the thread switch;
  - `OSLink`/`OSLinkFixed`;
  - no-ops: `__OSInitAudioSystem`, `__OSStopAudioSystem`, `__OSInitMemoryProtection` (real-mode BAT setup via `rfi`).

The whole OS scheduler, alarms, mutexes and message queues run recompiled, which proved simpler and more faithful than replacing the APIs. All replacements are listed in `config/GXEE8P/hle.txt`.

## Debugging aids (original list)

The full current list is "Debug variables (SR_*)" above.

**Debugging aids:**
- `SR_WATCHDOG=<seconds>`: records function entries and, when time runs out, dumps them, the last device accesses and every guest thread with its state, priority, wait queue and guest backtrace.
- `SR_SCREENSHOT=<frame>`: saves `build/shot_<frame>.png` (host).
- `SR_GXDEBUG=<frame>`: logs that frame's viewport/scissor/copies/draws.
- `SR_HEADLESS=1`: no window.
- `SR_INPUT="1500:START,1510:-START,..."`: scripted button presses at VI field counts.
- `SR_CARD=<path>`: memory card image.
- `SR_TEST_MENU=1`: start with the settings menu open.
- `SR_SHOTS=1` (Switch, via `env.txt`): save `shot_<frame>.bmp` at frames 300 … 7200 (off by default).
- Switch: `env.txt` in the SD folder sets these variables; `SR_WATCHDOG_NOTRACE=1` skips the function trace.
- `tools/showfn.py ADDR|NAME`: print a function's disassembly from dtk's output.
- `build/run.ps1 <seconds>` (local helper, not committed): rebuild + run + summarize.

## Plan: ahead-of-time specialized shader cache

Implemented since (tools/gen_shaders.py, gx_shader_key.c); the original plan:

**Plan: ahead-of-time specialized shader cache (the GPU fix; user's preferred "built in advance" approach)**
- Why: the fragment ubershader costs ~33 ms for ~120 draws at 2x. deko3d has no runtime GLSL compiler, so specialized shaders must be compiled with uam at build time.
- **Key:** the fragment state that drives control flow:
  - TEV stage count; per stage: order (map, coord, enable, channel), color/alpha env, ksel, swap selects, IND_CMD;
  - the indirect stage count and IREF; alpha test compare/logic; the EFB alpha-update rules.
  Register *values* (TEV colors, konst, matrices, alpha refs) stay uniforms, so one shader covers many materials.
- **Collect:** the PC build logs every new key (`SR_SHADER_KEYS=<file>`). Play through the game, or merge key files from the user's Switch (`sdmc:/switch/sonicriders/shader_keys.txt`, written when a key misses the cache).
- **Build:** `tools/gen_shaders.py` emits straight-line GLSL per key (the gx.frag logic with every select resolved at generation time), compiles each with uam, and embeds a sorted key → dksh table. The GL backend can use the same generated GLSL to check equivalence against the ubershader on PC.
- **Runtime:** look up the key per state; on a miss use the ubershader and record the key for the next build.

## Testing without hardware: Eden v0.1.1 (original notes)

**Testing without hardware: Eden v0.1.1** (`build/ext/eden-v0.1.1/app`, gitignored; portable `user/` folder)
- **Use v0.1.1.** v0.2.1 aborts in `dkDeviceCreate`, even with devkitPro's stock `deko_basic` sample.
- Keys: Eden needs the user's own console keys in its portable `user/keys/` folder. Never commit them: `*.keys` is gitignored and refused by the guard (tools/check_no_game_data.py).
- SD card: `user/sdmc/switch/sonicriders/` holds the NRO, an ISO copy, saves and the log.
- Run: `eden-cli.exe "user\sdmc\switch\sonicriders\sonicriders.nro"`. Don't start it minimized: rendering stalls. The user is fine with the window being visible.
- Eden has no process handle for homebrew, so guest memory uses the `svcMapMemory` fallback there.
- The port's own `shot_<frame>.bmp` self-captures are how to look at frames. Don't screen-capture the desktop: other windows end up in the image.

**Test scripts (scratchpad, not committed; recreate if missing):** `swtest.sh` (build the Switch NRO, stop on build errors, deploy to Eden, run, filter the log), `eden_run.ps1` (start and kill eden-cli), `bmp2png.py`, `bmpstat.py`.

## Architecture decisions (2026-10-15)

| Decision | Choice | Why |
|---|---|---|
| Recompiler | Fork ZeldaWWHDRecomp's translator (`ppc2c.py`, `crlive.py`, `leaflocal.py`, `ppc.h`, fres/frsqrte tables; MPL-2.0, upstream commit f10b566) + new GameCube front end `tools/recomp/gcrecomp.py` | Espresso and Gekko share the 750 user ISA, incl. paired singles, GQR quantization and `dcbz_l`. Its inline C with register-in-locals passes is proven at 30 fps on Switch hardware. **DolRecomp** (github.com/ExpansionPak/DolRecomp, GPL-3.0) was evaluated: it supports DOL+REL natively but is emulator-style (full MSR/exception model, callback memory access, a helper call per op), which is slower on ARM. Kept as a reference only. |
| Function discovery | decomp-toolkit v1.8.4 analysis (`config/GXEE8P/config.yml`), with `detect_strings: false` (a dtk bug in REL sec 2) | Understands MWCC idioms and REL relocations. Not used for matching. |
| Jump tables | Structural: a data object whose words all point mid-function into one function. The recompiler reports any `lwzx/mtctr/bctr` without a table. | dtk names only some tables `jumptable_*` (others like `@740`). |
| REL placement | Linked statically at `REL_BASE = 0x81800000`, just above the 24 MiB of MEM1 (the runtime backs 28 MiB), by the recompiler and identically at runtime (`loader.c`). The game still loads and "links" its own copy from its heap, and the native OSLink points that copy at the fixed image. | Fixed addresses for recompiled code, and the game keeps exactly its normal memory. (First tried 0x81500000 with ArenaHi lowered, which would have cost the game ~2.5 MB.) |
| Guest memory | Window indexed by physical address, `ea & 0x3FFFFFFF` (folds the 0x8/0xC mirrors; locked cache → `0x20000000`). Loads/stores in `0xC8000000–0xCFFFFFFF` go to `mmio.c`. | One AND + one compare per access, and every hardware access, incl. inline GX FIFO writes in game code, is caught. |
| HLE boundary | **Native:** OS, DVD, VI, PAD, SI/EXI (stubs), CARD, DB/TRK (stubs), AI, AR/ARQ, DSP (mailbox + AX ucode HLE). **Recompiled:** GX (+ native FIFO/CP/PE model), AX, MTX, MSL, CRI middleware, game. | OS must be native: context switches, rfi, interrupts. GX on GameCube is a stream of FIFO register writes, partly inlined into game code, plus display lists. A FIFO parser is needed anyway, so the FIFO is the natural boundary rather than ~200 GX functions. AX via its DSP protocol reuses the real AX library's exact semantics. |
| Renderer | GX FIFO → state → shared GLSL (TEV ubershader first); backends: desktop OpenGL (dev/testing), deko3d (Switch, shaders via uam) | Lets me test without hardware; the user's requirement of deko3d on Switch holds. |
| Host toolchain | clang 23 (winget LLVM) + MSVC 2022 libs; Switch: devkitPro devkitA64 GCC 16, libnx, deko3d, uam (already installed) | `musttail` and the GNU builtins are required by the generated code. |
| Disc access | The runtime reads the user's ISO directly (FST lookup in `disc.c`) | The player only copies their own ISO; nothing is extracted or shipped. |
| License | Project code MPL-2.0 (compatible with the forked files) — **[tentative]**; third_party/LICENSE-MPL-2.0 | Forked files must stay MPL-2.0. |

### SDK map of main.dol (from dtk names + Dolphin `totaldb.dsy` signatures via `tools/sigmatch.py`)
Matching: 804 confident matches (≥0x24 bytes, exact size). Smaller matches are unreliable (e.g. a lone `blr` "matches" game methods from other titles).

| Range | Library |
|---|---|
| 80003100–800055E0 | `.init` (`__start`, `__init_*`, memset/memcpy) |
| ~80005590–80009800 | game bootstrap, `main` @ 80007A48 |
| 800097CC–80011FF4 | OS (+ DB) |
| 80011FF4–80016624 | DVD |
| 80016624–80018700 | VI |
| 80018728–80019ECC | PAD |
| 8001A7F4–8001C5A0 | AR / ARQ |
| 8001C5A0–80025E5C | unnamed, ~39 KB (AX/AXFX/MIX likely; no signatures) **[uncertain]** |
| 80025E6C–800269E8 | DSP |
| 800269E8–8002D38C | CARD |
| 8002DA78–80034F2C | GX |
| 80034F4C–80042BC8 | MSL / runtime |
| 80042BC8–80046330 | EXI, SI |
| 80046330–8004C4D4 | MetroTRK |
| 8004C4F8–8004DA00 | MTX, DB |
| 8004DA00–800E2F20 | ~600 KB unnamed: CRI ADX/Sofdec/SNDF + other middleware **[uncertain]** |

`main` (80007A48):
1. Calls OSInit, `fn_8000629C`, `fn_80063204`, `fn_80006CD0`, `fn_800E2918`.
2. Loads `SonicR.str` and `_Main.rel` with `fn_80007CE8(name, 1)`.
3. Calls `fn_8000DCBC` (likely OSSetStringTable) and `fn_8000E288` = OSLink.
4. Jumps to `module->prolog`.

## Phase 1 — Recon (2026-10-15)

### Backup inventory
The backup dir contained exactly one file. The original was moved to a local backup folder on 2026-10-15 at the user's request (same-drive rename; SHA-1 re-verified afterwards; read-only attribute set). Working copy: `baserom/` (gitignored).

| File | Size (bytes) | Format |
|---|---|---|
| `Sonic Riders (USA) (En,Ja,Fr,De,Es,It).iso` | 1,459,978,240 | GameCube GCM (full-size raw disc image) |

### Hashes
| File | SHA-1 | MD5 |
|---|---|---|
| Disc image (`.iso`) | `ce0cecf57034bef8bab7d6f588ce92c85a849613` | `80155de587c0233e39ab545c2e771749` |
| `main.dol` (extracted, 1,195,200 B) | `e90a13504974b917a675d59f07014e0e817c5e5d` | `46874b58c6f1372b3bfce69ca6d8c6d6` |
| `_Main.rel` (extracted, 2,572,344 B) | `1607fbb1ccabf116642c3c59ee9b21c304a1db26` | `35a2a787ed42f1f1ba82570bb3a67905` |
| `SonicR.str` (extracted, 40 B) | `caf827eb8c510744c0825304f68ae89250bb11a3` | `e4fb96ae3bf1404ebbdacab1d059e79d` |

The copy in `baserom/` has the same SHA-1 as the original. [confirmed]
**Redump match** [confirmed 2026-10-15]: redump.org/disc/7133/ "Sonic Riders", serial DL-DOL-GXEE-USA, edition "Original, Player's Choice". MD5 `80155de587c0233e39ab545c2e771749` and SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613` match ours exactly. Size 1,459,978,240, CRC-32 `1dabe6ee`. So the dump is clean and verified, and is the same disc as both the original and the Player's Choice pressings.
Other Redump entries, for reference only: PAL DL-DOL-GXEP-EUR (/disc/19825/), JPN DL-DOL-GXEJ-JPN (/disc/9328/).

### Disc header [confirmed]
- Game ID `GXEE8P` (G=GameCube, XE=Sonic Riders, E=USA, 8P=Sega), disc 0, revision 0, region code 1 (NTSC-U)
- Internal title `SonicRiders`
- DOL offset `0x20300`, FST offset `0x144000` (size `0x72b1`)
- Apploader date `2004/11/10`, entry `0x81200258`
- FST: 1,669 files, all at the disc root. By size: SFD (Sofdec video) 515 MB, extensionless game data 500 MB (1,563 files), ADX audio 294 MB, AFS archives 79 MB, `.dat` 9 MB, one `.rel`, one `.str`, one `.bin`, `opening.bnr`. Full listing: `baserom/fst_listing.txt`.

### Executables
The code is split across two executables:

**1. `main.dol`**: DOL format, PowerPC 750CL "Gekko" (32-bit, **big-endian**). [confirmed]

| Section | DOL file offset | Load address | Size | Probable name |
|---|---|---|---|---|
| text0 | 0x000100 | 0x80003100–0x800055E0 | 0x24E0 | `.init` |
| text1 | 0x0025E0 | 0x800055E0–0x800E2F20 | 0xDD940 | `.text` |
| data0 | 0x0DFF20 | 0x800E2F20–0x800E2F40 | 0x20 | `.ctors` (all zero) |
| data1 | 0x0DFF40 | 0x800E2F40–0x800E2F60 | 0x20 | `.dtors` (all zero) |
| data2 | 0x0DFF60 | 0x800E2F60–0x800ED2E0 | 0xA380 | `.rodata` |
| data3 | 0x0EA2E0 | 0x800ED300–0x80125E00 | 0x38B00 | `.data` |
| data4 | 0x122DE0 | 0x8021B8C0–0x8021BB40 | 0x280 | `.sdata` |
| data5 | 0x123060 | 0x8021C340–0x8021CFA0 | 0xC60 | `.sdata2` |
| bss | — | 0x80125E00–0x8021CFA4 | 0xF71A4 | `.bss` (+`.sbss`/`.sbss2`, interleaved) |

Entry point: `0x80003154`. Its code is the standard Metrowerks `__start` (`bl __init_registers; bl __init_hardware; ...`). [confirmed by byte pattern]
Section names come from the standard MWCC layout. [likely]

**2. `_Main.rel`**: REL module (relocatable, loaded at runtime by the DOL). Holds most of the game code. [confirmed]
- Module id 1, REL version 3, 14 section slots, align 0x20, bss-align 0x40, fixSize 0x1FF360
- Prolog: section 1 + 0x1BC310. Epilog and unresolved handlers: none (0)
- Imports: relocations against itself (module 1) and against the DOL (module 0)

| Section | File offset | Exec | Size | Probable name |
|---|---|---|---|---|
| 1 | 0x0000BC | yes | 0x1C10DC (1.8 MB) | `.text` |
| 2 | 0x1C1198 | no | 0x16914 | `.rodata` [uncertain] |
| 3 | 0x1D7AC0 | no | 0x27890 | `.data` [uncertain] |
| 4 | — | no | 0xBA6F4 | `.bss` |

- `SonicR.str` contains `D:\Projects\SonicR\elf\master\_Main.plf`. This is the makerel string table: the REL's original link output was `_Main.plf`, in project folder `SonicR`. [confirmed]

### Toolchain fingerprints
- **Compiler: Metrowerks CodeWarrior (MWCC) for GameCube.** Evidence: the `Metrowerks Target Resident Kernel for PowerPC` string, `Metrowerks CW runtime library initializing default heap` (from `GCN_Mem_Alloc.c`), the MW `__start`, and the section layout. [confirmed it's MWCC; **exact version unknown**]. Given the late-2004 build dates, it is probably one of the GC 2.x releases (2.6/2.7 era), but **this is a guess** and can only be settled by test-compiling functions.
- **Language: probably C, not C++.** `.ctors`/`.dtors` are empty, with no `__init_cpp_exceptions` entry. There are no mangled names, no RTTI, and no `extab`/`extabindex` sections. [likely]
- **Nintendo Dolphin SDK, revision 0x2301:**
  - OS and GX: release build Nov 10 2004
  - VI: Apr 7 2004
  - DVD, PAD, AI, AR, ARQ, AX, DSP, CARD, EXI, SI: Apr 5 2004
  - These are release (non-debug) libraries. OS and GX were patched later, which is consistent with a late-2004 SDK patch. [confirmed strings]
- **CRI Middleware** (audio/video), all in the DOL:
  - ADXT/GC 9.31, ADXGC 1.21, ADXGCSDK "20Apr2004Patch1", ADXF 7.18, CVFS 2.37, LSC 2.18, SJ 6.28, SVM 1.54, MFCI 1.09, GCCI 1.09, AXRNA 1.04, SKG 0.64 (built Oct 8 2004)
  - Sofdec: MWSFD 3.33, SFD 1.947, SFH 1.19, MPV 1.933, MPS 1.924, DCT 1.945, SFX 2.08, SUD 0.05, CFT 1.57 (built Sep 22 2004)
  - **SNDF Ver.2.21a, built Dec 5 2005**. This is the newest timestamp in the binary. [confirmed]
- Debug/RTOS remnants: TRK/ODB exception hooks in OS (`ODBExceptionDestination`). These are normal for SDK builds.

### Symbols / map files
- **No map files, no ELF, no debug symbols on the disc.** [confirmed: only one `.rel`, no `.map`/`.elf`/`.plf`]
- Only 3 source filenames leaked: `OSThread.c`, `dvdfs.c` (SDK), and `GCN_Mem_Alloc.c` (MW runtime). There are no game-side source filenames.
- The game code has very few strings: about 900 in the REL, mostly asset names (`*.ADX`, `*.SFD`) and save-file names (`SONICRIDERS_GAMEDATA_%02d`, `SONICRIDERS_GHOSTDATA_%02d`).
- Practical consequence: SDK/MSL/CRI functions can be identified by signature matching against known SDK libraries. **Game functions must be named by hand.**

### Leads
- **GameCube NPDP prototype**: the Sonic Retro article does **not** mention symbols, a map file, an ELF, or debug info. [confirmed, but absence of a mention is not proof of absence]
  - Live wiki is behind an Anubis bot-check, so I read the Wayback snapshot of 2025-01-18. The 2026-03-05 snapshot captured only the bot-check page.
  - The article's full substance: found on an NPDP cartridge by MSX, dumped March 2021, "appears to be near-final, if not final".
  - Hidden Palace has a matching entry (hiddenpalace.org/Sonic_Riders_(NPDP_prototype)), not inspected. The Sonic Retro discussion thread was not readable (bot-check).
  - Nothing was downloaded.
- **PS2 prototype 0.931 (7 Oct 2005)**: a community restoration (Sewer56 index → Sonic Retro thread 39828) lists "Partial Symbols (Function, Variable Names)" from a debug executable. [confirmed that the claim exists; not inspected]
  - It is a **different platform** (MIPS, different compiler), so it is useless for byte-matching.
  - It *might* help name functions and structs by cross-referencing logic. [uncertain]
- **Sewer56.SonicRiders / Riders.Tweakbox** (github.com/Sewer56): reverse-engineered PC-version structs and data, with GameCube file-format tools. Possible naming reference. [uncertain]
- **Sonic Adventure DX decomp**: verified on github.com directly (via the GitHub API): https://github.com/doldecomp/sadx. Description "Sonic Adventure DX", not archived, last push 2024-08-01, 48 stars, no license file. [confirmed]
  - It is a dtk-template project.
  - Linker GC/2.0; libraries pinned to GC/1.2.5n, 1.3, and 1.3.2. A 2003 game from a different team, so **not evidence** for Riders' compiler. Use it as a structural reference only.
  - (The earlier link I gave pointed at a mirror, gitblind.noratr.app. Disregard it.)
- A Windows PC version of Sonic Riders (2006) exists. Its binary could help with naming and logic cross-reference, though not with matching. [uncertain]
- No existing Sonic Riders decomp found in a quick search (2026-10-15).

### Scale estimate
- Code to decompile: DOL `.text` 0.9 MB (much of it SDK + CRI + MSL) plus REL `.text` 1.8 MB (game code). That is roughly 680k PowerPC instructions in total.
- The SDK portions can likely reuse existing community decomps of Dolphin SDK 2004-era libraries. The CRI middleware has no public source.

### Re: end goal (Switch)
A *matching* decomp yields C source that rebuilds the GameCube binary. Running it natively on Switch is a **separate port project** on top of that:
- Replace GX/OS/DVD/PAD/AX/CRI calls with a portability layer (e.g. SDL + OpenGL/Vulkan)
- Handle endianness of all asset data
- Handle 32-bit pointer assumptions
- Replace the closed-source CRI ADX/Sofdec decoders

This should be planned for, but it does not change Phases 2–4.

### Files in `baserom/` (gitignored, never commit)
- the ISO copy
- `main.dol`, `_Main.rel`, `SonicR.str`
- `fst_listing.txt`, `recon_strings_*.txt`

## Phase 2 — Tooling plan for a matching decomp (2026-10-15) — SUPERSEDED by the recompilation strategy above; kept for reference

### Recommendation: decomp-toolkit + dtk-template + objdiff
- **decomp-toolkit (dtk)**: https://github.com/encounter/decomp-toolkit, latest v1.8.4 (2026-09-09). The de-facto standard for GC/Wii decomps.
  - Analyzes DOL **and REL**, finds function boundaries, and detects MW runtime and SDK signatures, strings and objects.
  - Splits code into relocatable `.o` objects from `symbols.txt` + `splits.txt`.
  - Rebuilds DOL and REL (`elf2dol`, `rel make`), and reads ISOs directly via its VFS.
- **dtk-template**: https://github.com/encounter/dtk-template, CC0 license.
  - `configure.py` generates a ninja build. It auto-downloads pinned versions of dtk, objdiff-cli, PPC binutils, sjiswrap and the MW compiler pack into `build/` (project-local, not system-wide).
  - Per-library `mw_version` and cflags, REL support built in, `build.sha1` verification, progress reports.
  - The template currently pins dtk v1.8.3, objdiff v3.6.1, compilers 20251118, binutils 2.42-2. These can be bumped.
- **objdiff**: https://github.com/encounter/objdiff, latest v3.8.2 (2026-09-29). Per-function diff of our compiled `.o` against the target `.o` that dtk split out.
  - Understands PPC relocations, so address differences don't show as false mismatches.
  - The CLI is used by the build for progress; the optional GUI gives a live diff that updates on save.
- **Rejected**:
  - splat: N64/PS1/PS2-oriented, no DOL/REL support.
  - asm-differ: redundant with objdiff on GC.
- **Optional, later**:
  - m2c: produces PPC → C first drafts.
  - Ghidra + a GameCube DOL/REL loader: for reading and naming.
  - decomp.me: **note** that scratches there are public, so uploading asm publishes copyrighted code.

### How the build/verify works (differs from "reassemble from .s")
dtk does not reassemble text `.s` files. Instead:
1. It splits the original binaries into relocatable objects at the split points (`build/GXEE8P/obj/...`).
2. The build links those objects plus any matching compiled C with the original Metrowerks linker (`mwldeppc`).
3. It runs `dtk elf2dol` for the DOL and `dtk rel make` for the REL, then checks the SHA-1 of the outputs against `config/GXEE8P/build.sha1`.

As decompiled C files reach 100% match, they replace the extracted objects. Disassembly (`.s`) is still written to `build/GXEE8P/asm/` for reading.

Verification targets `main.dol` + `_Main.rel` hashes (recorded above), **not the ISO**. Rebuilding the ISO itself is out of scope and unnecessary.

### Compiler-version test plan
Expect **several compilers** in one binary:
- Nintendo's prebuilt SDK libraries: typically GC/1.2.5n for SDK libraries of this era. [likely, to be verified]
- CRI's prebuilt libraries: their own compiler.
- Game code + MSL/runtime: whatever CodeWarrior Sega used.

Each library gets its own `mw_version`. Steps:
1. **Structural fingerprints, no compiling.**
   - Linker: `.ctors`/`.dtors` layout and padding (the template documents a GC 1.0–2.6 vs 2.7+ difference), section alignment gaps, and the odd absence of `__init_cpp_exceptions` in `.ctors`.
   - MSL/Runtime: once dtk labels the runtime functions (`__save_gpr`, `memcpy`, `__div2u`, printf internals…), compare them against existing matched MSL/Runtime builds for each CW version. The runtime ships with the CW install, so a byte-match pins the toolchain release.
2. **Empirical sweep.**
   - Pick 6–10 REL game functions that exercise version-sensitive codegen:
     - int↔float conversion constants
     - switch jump tables
     - loops (unrolling/strength reduction)
     - struct copies
     - small inlined helpers
     - register-pressure prologues (`stmw` vs `_savegpr`)
   - Write each in C once. Compile with every candidate in the compiler pack (GC/1.3.2, 2.0, 2.5, 2.6, 2.7, 3.0a3, 3.0a5.2…) crossed with plausible flag sets (`-O4,p` / `-O4,s` / `-O3`, `-inline auto|deferred`, `-str reuse|pool`, `-sdata`/`-sdata2` thresholds).
   - Score each combination with `objdiff-cli diff` (match %).
   - The version/flag set that gives 100% across *all* samples wins. If several do, take the range and keep sampling.
   - I'll script this sweep in `tools/` and record the results matrix here.
3. The **linker** version is chosen separately: the one whose linked output reproduces the section layout/padding byte-for-byte.

## Open questions
0. (recomp) Which unnamed DOL functions are OSLink, OSSetStringTable, the AX library, and the file loader `fn_80007CE8`? Needed for hle.txt.
0b. (recomp) Does any code jump/call through a computed address that is not a dtk function entry? The runtime aborts with "dispatch to X: not a function entry"; X so far: only the null callback in OS thread code (expected, OS will be native).
0c. (Switch) Memory budget: guest MEM1 24 MiB + ARAM 16 MiB + generated code (~tens of MB) is far below SwitchWakerHD's; applet mode may even suffice. **[uncertain until hardware test]**
1. (matching decomp only, no longer needed) Exact MWCC version(s) and flags.
2. Is the game code really pure C? Check for C++ patterns in the REL (vtable-like tables, `__ptmf`, etc.).
3. Precise REL section names (sec 2/3 ordering of `.rodata`/`.data`).
4. ~~Does the NPDP prototype have symbols?~~ Not documented as having any (see Leads).
5. ~~Does the disc hash match Redump?~~ Yes (redump.org/disc/7133/).
6. REL rebuild quirks [uncertain]:
   - the module name lives in the separate `SonicR.str` (nameOffset 0, nameSize 40)
   - no epilog/unresolved functions
   - Need to confirm that `dtk rel make` reproduces these byte-for-byte.
7. Why are `.ctors`/`.dtors` empty with no `__init_cpp_exceptions`? Possibly a custom runtime or link setup.

## Naming decisions
(none yet)
