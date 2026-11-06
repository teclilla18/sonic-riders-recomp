# State (2026-11-06)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).

**Project paused by the user after 2026-11-06** (Vita paused too; resume only when asked). **Decision (user,
2026-11-06): screen recording on Android is not supported; no more work on it** (README and INSTALL say so).
**Next focus: online play (phase 3/4, see "Online"), set up and tested without the user; the user's hardware
tests (two PCs, Switch-Switch) come last.**

Latest release: **v0.4.2** (2026-11-05, tag at b790304): Android crash reports (next-launch dialog + report in
`Download/SonicRiders`); PC and Switch unchanged since v0.4.1. Cold Windows Sandbox PC + Android PASS on b790304
(both folders) [confirmed]; crash dialog checked on the S23 Ultra and the emulator [confirmed]. Not yet seen by a
user. Vita code: branch `vita` only.

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

- **Open, known issue since v0.4.0: device lost (crash) while screen recording.** 2026-11-06 (on `main`, not
  released): the renderer now rebuilds itself after a device loss (`device_recover`; ~250 ms device + ~110 ms
  pipelines; fake losses in the intro and a race recover with a right picture [confirmed]; normal perf test
  unchanged: 59.4-59.7 / 59.9 fps, race GPU 12.3-12.5 ms in an alternating A/B [confirmed]). With `screenrecord`
  the rebuilt device is lost again every ~10 frames, so after the 4th loss in 10 s it still ends in the fatal
  error: **not fixed** (every recording run today). Bisect leads (docs/notes/android.md "Open: device lost"): only
  "one pipeline per pass" and "depth writes forced on" survive, both with a wrong picture [uncertain lead: depth
  state].

- **Crash reports (released in v0.4.2):** tested on the S23 Ultra (missing disc -> dialog, report, Close; no
  dialog after a normal stop) and the emulator (also kill -SEGV). Reddit "crashes on launch": ask for the report.
- Runs on the S23 Ultra (Android 17): Vulkan renderer, picture = PC on 16 det-route frames, pipeline prewarm,
  background/resume, touch controls, Xbox Series X pad over Bluetooth. [confirmed]
- Perf (S23 Ultra, Android 17, `SR_PERFTEST`): **specialized fragment shaders** (2026-11-03, tested by A/B on the
  phone, not yet seen by the user): 1P demo 55.1 → 59.3 fps, 2P demo 58.4 → 59.8 fps; GPU 1P race ~12-13 ms/frame
  (was 14-18), title 1.7 ms (was ~10). Pictures identical to the ubershader. Game thread ~5 ms/step.
- Scope (user): local split screen up to 2 players with pads; 3-4 players only online (one screen each).
- Touch pictures `android/touch_icons/*.png` are the user's art, committed (guard `OWN_ART` SHA-1s).
- **Touch/controller round 2: accepted by the user (2026-11-03, S23 Ultra)**: stick, menu taps, START, triggers.
  App icon: the user's own .ico built locally with `--icon` (never committed). Right stick → C-stick: code only.
- **Next after the crash:** GPU needs a profiler (packed vertices tried: no gain); leads in notes/android.md: EFB passes on the tiler, per-draw UBO
  size), pre-rotation, 2P with two pads.

## After Android (user's wish list, 2026-11-03)

Picture enhancements (resolution scale on PC/Android, bloom, HD texture replacement). **PS4/PS5 port: ON HOLD**
(user: two jailbroken PS4s, no PS5; research in docs/notes/ps4-ps5-research.md). **PS Vita: researched** (docs/notes/vita-research.md):
Probe V1a on the user's PCH-2000 (2026-11-04): plays headless, game thread ~1.6x too slow for 30 fps [confirmed].
Next: V1b (GXM draw microbench), game-thread profile. Details: docs/notes/vita-research.md (branch `vita`).

## Online play: NEXT FOCUS (paused 2026-11-03 for Android; resumes next session)

Where it stands (details: docs/ONLINE.md "Phase 3"; all on `main`, nothing half-done in the tree):
- Done: determinism (`SR_DETCLOCK`), input record/replay, lockstep with desync check (`SR_NET`), generated online
  save, own view (`SR_VIEW`). One PC: 8,400 frames identical on the 2P race route. PC-Switch session (2026-10-30):
  identical to frame 6917, then a desync at 6919 from the sign of a NaN (x86 vs ARM).
- Next when resumed, in order: (1) PC-PC on two PCs and Switch-Switch sessions (2P race route); (2) phase 4 NaN fix
  on x86 at the operations that create a NaN (not at stores: tried, reverted); (3) turn the host-clock alarm backstop
  off in sessions (fired once in a 585 ms PC stall).
- Untested: Android NaN signs (arm64 should match the Switch, x86_64 the PC).

## Open items, in priority order

1. Online play phase 3/4 (next focus, see "Online"). Android screen recording: not supported (user decision
   2026-11-06; recovery from single device losses is in). Android GPU cost later. Story/other modes may use other values of `8021B8C0` (watch `[touch] game mode` lines).
2. #12 PC at 13 fps (v0.3.4): read the attached log (GPU? vsync? software GL?).
3. #1 stock-clock performance: 60 fps in busy views needs ~20% less game-thread work. HISTORY 2026-10-26.
4. #2 3-4 player races at 30 fps (game core ~17-19 ms/frame in 4P). HISTORY 2026-10-23 (hw25), 2026-10-22.
5. Audio in Auto at stock clocks crackled (hw27); mixer priority raised. Needs a log: "mixes started late" ~0, 200 lists/s. HISTORY 2026-10-25 (evening).
6. #3 speed blur weaker than GameCube: padded copies fixed most of it on PC; the overlay/particles may still differ. HISTORY 2026-10-23 (night), 2026-10-26 (night).
7. #6 rare "Now Loading" hang: not seen since the progressive disc DMA fix (2026-10-18), not proven fixed.
8. #4 story cutscene monitor garbled (frame ~18000 of the story route); unverified against Dolphin.
9. #7 lens flare: EFB Z peeks return 0. HISTORY 2026-10-16 (b).
10. Switch `[applet]` hook never logs; `rt_log` lines from different threads can interleave. HISTORY 2026-10-23 (hw22).
11. Untested on hardware: single sideways Joy-Con mapping, mods on the Switch (`sdmc:/switch/sonicriders/mods`).
12. Small graphics: Player Entry sprite ~8 levels lighter than the real game; faint see-through billboards; dark
    translucent dome over the Metal City start gate; fog range adjustment (BP 0xE8-0xED) and dynamic indirect
    matrices not implemented.
13. #9 hardware reports wanted (Lite, original model, docked); #10 PNG screenshots (good first issue).
14. Later / on request: mods (stats by name, tracks), Super Shadow (verify the racer's gear-id field first),
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
