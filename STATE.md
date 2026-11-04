# State (2026-11-04, evening)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).

Latest release: **v0.4.0** (2026-11-04): the Android port (`android` merged into `main`) plus the online-play
groundwork and the performance test. Vita probe code lives on branch `vita` (not in the release).

**v0.4.0's Android download is broken for everyone** (APK step: "unrecognized arguments"); fixed on `main`
with four more clean-install fixes found by the new Windows Sandbox test (`tools/sandbox_test.py`, 2026-11-04):
cold PC and Android installs PASS on 174b9811 (zips v0.4.1-rc5, not published). **Next: release v0.4.1 once the
user agrees** (rerun the cold sandbox test on the final commit first; procedure: notes/build-and-test.md).

## Windows host build (the fast test loop)

- Plays the whole game: intro FMV, menus, saves, story mode (cutscenes with voices), 1-4 player races at 60 fps,
  with sound (native AX microcode, `ax.c`, mixed on a worker thread). [confirmed]
- Controllers: keyboard + SDL gamepads (Xbox One S tested by the user); round 25% stick dead zone (2026-10-23).
- Settings menu: Dear ImGui (F1 / controller Back held 0.5 s); tabs Display, Performance, Controls, Gameplay, Mods,
  About. Frame rate modes: 60 / 30 (full speed) / Auto (default).
- Mods layer 1 (file replacement, `mods/`) and stat mods (`[gear.*]`, `[memory]`, `[gecko]`): working on PC; not
  verified that a race uses changed gear stats.
- `SR_DETCLOCK` determinism: whole-run identical memory every frame on PC, and PC = Switch on the det route.
- Perf test (2026-10-30, 3 runs): both demo races 59.9 speed/fps, game thread 2.4 ms per step.
- Open report #12: a player's PC runs races at 13 fps (v0.3.4, log attached, not analysed yet).

## Switch build

- Runs the whole game from the Homebrew Menu in title mode, or from the forwarder NSP on the home screen (works on
  hardware, hw16). Sound works; sleep/wake fixed (hw22).
- Graphics: face culling, see-through surfaces, palette address bits (#5, UI colours), specular NaN and the uam
  lighting miscompile (#11, teal characters, fixed in v0.3.3), padded copy textures (dark picture), fog: all fixed.
  Characters match Dolphin on hardware (2026-10-29).
- **Overclocked** (CPU 1785 / GPU 1152 / MEM 2133): 1P races ~60 (perf test: 58.1 fps 1P demo, 56.4 fps 2P demo,
  full speed, game thread 13.5 ms/step); 3P mostly 55-60 with dips to ~48 at 6,000+ draws (hw23/24); 4P a steady
  30 fps (hw25, game core ~17-19 ms of work per frame).
- **Stock clocks, handheld (384 MHz GPU profile)**: 30 fps mode is a steady 30 at full speed in every race block;
  60 fps mode drops to 30 in views with 5,000-7,000 draws because the game thread needs 15-21 ms per frame
  (hw33, hw36). GPU 7-13 ms per race frame after the shader-key fix (v0.3.2).
- Specialized shaders cover every state (hw36: 2,563,268 of 2,563,268) once keys ignore unused bits.

## Last hardware tests

- **2026-10-30, overclocked Switch (switch-remote):** det route and a replayed PC recording identical to the PC on
  all 7,700 frames; PC-Switch lockstep desync at 6919 (NaN sign, x86 vs ARM, see Online). Perf record:
  `perf/results/2026-10-30-switch.json`.

## Android port (released in v0.4.0; plan: docs/ANDROID_PLAN.md; history: HISTORY 2026-11-03/04 entries)

- **Open, known issue in v0.4.0: device lost (crash) while screen recording**, also once on a warm phone without
  recording. Reproduces 4 of 4 with `SR_PERFTEST=1` + `adb shell screenrecord`. Four fixes tried and ruled out;
  next ideas (device-loss recovery, smaller present surface + pre-rotation): docs/notes/android.md "Open: device
  lost". Experiments patch: `build/old/android-devicelost-experiments-2026-11-04.patch` (local).

- Runs on the S23 Ultra (Android 17): Vulkan renderer, picture = PC on 16 det-route frames, pipeline prewarm,
  background/resume, touch controls, Xbox Series X pad over Bluetooth. [confirmed]
- Perf (S23 Ultra, Android 17, `SR_PERFTEST`): **specialized fragment shaders** (2026-11-03, tested by A/B on the
  phone, not yet seen by the user): 1P demo 55.1 → 59.3 fps, 2P demo 58.4 → 59.8 fps; GPU 1P race ~12-13 ms/frame
  (was 14-18), title 1.7 ms (was ~10). Pictures identical to the ubershader. Game thread ~5 ms/step.
- Scope (user): local split screen up to 2 players with pads; 3-4 players only online (one screen each).
- Touch pictures `android/touch_icons/*.png` are the user's art, committed (guard `OWN_ART` SHA-1s).
- **Touch/controller round 2: accepted by the user (2026-11-03, S23 Ultra):** stick (home north-east, calmer in
  menus via game mode `8021B8C0`), menu taps and Close, START button, triggers in races (log: RT reached 32767 and
  clicked; LT only light presses, stayed analog). App icon: the user's own .ico, built locally with `--icon`
  (box art, never committed; default for players is the save icon). Right stick → C-stick: checked in code (same
  round 25% dead zone, full range, up = higher, sent unchanged in the SI poll); not checked in a game screen that
  uses the C-stick.
- **Next after the crash:** GPU needs a profiler (packed vertices tried: no gain); leads in notes/android.md: EFB passes on the tiler, per-draw UBO
  size), pre-rotation, 2P with two pads.

## After Android (user's wish list, 2026-11-03)

Picture enhancements (resolution scale on PC/Android, bloom, HD texture replacement). **PS4/PS5 port: ON HOLD**
(user: two jailbroken PS4s, no PS5; research in docs/notes/ps4-ps5-research.md). **PS Vita: researched** (docs/notes/vita-research.md):
**Vita probe (branch `vita`, 2026-11-04)** [confirmed, CPU 500 / GPU 222]: V1a headless runs the demos; after this
session's fixes (clock reads, 64-bit divide, log) 1P 21 steps/s, game thread ~40 ms/step (was 10.5 steps/s, 49
ms), 2P 22 steps/s, ~40 ms; still ~1.3x too slow for the game's 30 fps mode, GP thread 51-65 ms per drawn race
frame. Profile flat (PSMTX/PSVEC ~12%, OSEnableInterrupts 8%). V1b GXM microbench: 7,000 draws submit in 9.2 ms
(15.7 ms with a program switch per draw); libshacccg 78 ms per shader. A lost cnd_wait wake-up (hang) fixed by
bounded waits. Per-thread CPU (session 3): core 0 (guest code) is saturated, ~39-46 ms of CPU per step;
GP thread ~0.4 s/s, decode helper and audio ~0.3 s/s each; run-to-run noise ~±10%. Session 4: float-typed
single-precision locals in the recompiler implemented but **off** (`RECOMP_FLOATLOC=1` + `-DPPC_FLOAT_LOCALS=1`):
not bit-exact yet (PC det route differs from step 3547); next: find why, then measure on the Vita; then the audio
mixer's cheap wins (listed in the note). USB camera (udcd_uvc) is on: the Vita does not charge over USB. Details:
docs/notes/vita-research.md "Game-thread profile", "Probe V1b results", "Float-typed locals prototype".

## Online play: PAUSED (2026-11-03, for the Android port)

Where it stands (details: docs/ONLINE.md "Phase 3"; all on `main`, nothing half-done in the tree):
- Done: determinism (`SR_DETCLOCK`), input record/replay, lockstep with desync check (`SR_NET`), generated online
  save, own view (`SR_VIEW`). One PC: 8,400 frames identical on the 2P race route. PC-Switch session (2026-10-30):
  identical to frame 6917, then a desync at 6919 from the sign of a NaN (x86 vs ARM).
- Next when resumed, in order: (1) PC-PC on two PCs and Switch-Switch sessions (2P race route); (2) phase 4 NaN fix
  on x86 at the operations that create a NaN (not at stores: tried, reverted); (3) turn the host-clock alarm backstop
  off in sessions (fired once in a 585 ms PC stall).
- Android arm64 should behave like the Switch for NaN signs, x86_64 (emulator) like the PC: worth one det-route
  compare once Android boots.

## Open items, in priority order

1. Android device lost while recording (v0.4.0 known issue, see "Android" above); then GPU cost. Story/other modes may use other values of `8021B8C0` (watch `[touch] game mode` lines).
2. Online phase 3/4 (paused, see above).
3. #12 PC at 13 fps (v0.3.4): read the attached log (GPU? vsync? software GL?).
4. #1 stock-clock performance: 60 fps in busy views needs ~20% less game-thread work. HISTORY 2026-10-26.
5. #2 3-4 player races at 30 fps (game core ~17-19 ms/frame in 4P). HISTORY 2026-10-23 (hw25), 2026-10-22.
6. Audio in Auto at stock clocks crackled (hw27); mixer priority raised. Needs a log: "mixes started late" ~0, 200 lists/s. HISTORY 2026-10-25 (evening).
7. #3 speed blur weaker than GameCube: padded copies fixed most of it on PC; the overlay/particles may still differ. HISTORY 2026-10-23 (night), 2026-10-26 (night).
8. #6 rare "Now Loading" hang: not seen since the progressive disc DMA fix (2026-10-18), not proven fixed.
9. #4 story cutscene monitor garbled (frame ~18000 of the story route); unverified against Dolphin.
10. #7 lens flare: EFB Z peeks return 0. HISTORY 2026-10-16 (b).
11. Switch `[applet]` hook never logs; `rt_log` lines from different threads can interleave. HISTORY 2026-10-23 (hw22).
12. Untested on hardware: single sideways Joy-Con mapping, mods on the Switch (`sdmc:/switch/sonicriders/mods`).
13. Small graphics: Player Entry sprite ~8 levels lighter than the real game; faint see-through billboards; dark
    translucent dome over the Metal City start gate; fog range adjustment (BP 0xE8-0xED) and dynamic indirect
    matrices not implemented.
14. #9 hardware reports wanted (Lite, original model, docked); #10 PNG screenshots (good first issue).
15. Later / on request: mods (stats by name, tracks), Super Shadow (verify the racer's gear-id field first),
    C-stick look-back camera (camera writer not found), picture grading sliders, PGO, float-typed register locals.

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] Switch-Switch online session (two consoles, `SR_NET` in env.txt, 2P race route): no desync for a whole race.
- [ ] PC-PC session on two PCs (laptop + desktop) over the LAN.
- [ ] `SR_PERFTEST=1` at stock clocks, handheld: save a stock record in `perf/results/`.
- [ ] 4-player Metal City race with `SR_PROFILE=1`: fps, "GP thread busy", game-core idle share vs hw25.
- [ ] Auto mode at stock clocks in a race: listen for crackle; log "mixes started late" and the AX list rate.
- [ ] Sleep during a race and wake: does an `[applet]` line appear now?
- [ ] Speed blur and overall brightness in a race vs Dolphin (#3): screenshot via switch-remote.
- [ ] Story mode: watch one cutscene through and skip one with START; check the monitor (#4).
- [ ] A single sideways Joy-Con as player 1 and as player 2.
- [ ] Copy back `sonicriders.log`, `perftest.json`, `shader_keys.txt` (add new keys to the local key set).
