# State (2026-11-03)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md).

Latest release: **v0.3.4** (2026-10-29). Unreleased on `main`: online-play groundwork (record/replay, lockstep,
generated online save, own view) and the performance test (`SR_PERFTEST`).

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

- **2026-10-30, overclocked Switch, driven over Wi-Fi (switch-remote):**
  - `SR_DETCLOCK` det route: two Switch runs = two PC runs, identical memory on all 7,700 frames.
  - A PC input recording replayed on the Switch: identical on all 7,700 frames.
  - First PC-Switch lockstep session (Switch hosting, PC as player 2): identical up to frame 6917, then a desync at
    6919 caused by the sign of a NaN (x86 vs ARM). PC-PC and Switch-Switch are not affected.
  - Perf test record: `perf/results/2026-10-30-switch.json`.
- 2026-10-29: teal tint fix confirmed ("Are you ready?" screen and a Metal City race, 59.9 fps at the countdown).
- 2026-10-26, stock clocks (hw32-hw36): uniform-buffer XF reads -27% GPU time; decode helper; shader-key fix.

## Android port (branch `android`; plan: docs/ANDROID_PLAN.md)

- Milestones 1-2 done (2026-11-03): toolchain + probe APK (Vulkan chosen: 2.00 vs 8.06 ms CPU per 7,000 draws);
  game boots on the S23 Ultra (Android 17), phone = PC memory on 4,000 det-route frames. [confirmed]
- Milestone 3 (2026-11-03): `platform/android/render_vk.c` + `touch_android.c`.
  - **Picture now matches PC** on 16 det-route frames (intro, title, 1P and 2P demo races): mean difference 0.2-3.4
    levels (edges only, different screen size). The fix: Vulkan prelude `CLIP_Y 1.0` (was deko3d's -1: picture upside
    down, culling inverted, EFB copies read the wrong rows). [confirmed]
  - Pipeline hitches fixed: keys recorded in `vk_pipeline_keys.bin`, prebuilt on a helper thread at boot (35 in 68 ms
    with a warm driver cache); no pipeline built during play on the second run. [confirmed]
  - **Background/resume works**: HOME 15 s, back: game paused, swapchain remade, 59.9 fps at once. [confirmed, 1 run]
  - Xbox Series X controller works over Bluetooth (user, port 1; touch controls hide). [confirmed]
  - Perf (S23 Ultra, Android 17, `SR_PERFTEST`): game thread ~5 ms/step; 1P demo 55 fps, 2P demo 58 fps. The limit is
    the **GPU**: 18-24 ms per 1P race frame at 1x (`[vk] GPU` line); a 1-pixel scissor (`SR_VK_TINYSCISSOR=1`) still
    leaves 9-10 ms (per-draw/vertex cost: ~4.5 KB of UBO per state), so both pixel and per-draw cost matter.
    Render area limited to the used EFB part (no measurable change). First heavy race frame still ~90 ms (GPU).
  - Scope (user, 2026-11-03): local split screen up to 2 players with pads; 3-4 players only online (one screen each).
  - Touch pictures `android/touch_icons/*.png` are committed (user's own art; guard exception pinned by SHA-1 in
    `tools/check_no_game_data.py` OWN_ART: a changed picture needs its new hash there).
- **Next: the user's touch/controller feedback (2026-11-03), all not done yet:**
  1. DONE (2026-11-03): new knob picture from the user (rings centred), cut out of its baked-in checkerboard on a
     fitted circle, 256 px, same opacity and movement; installed on the phone, not yet looked at by the user.
  2. Stick home position a little north-east; stick less sensitive in the game's menus (bigger dead zone / curve).
  3. PAUSE: a circle with "START" written inside, centred.
  4. Overlay menu by touch is unreliable (taps sometimes ignored); its "Close" does not close the menu (only brings
     the touch buttons back); hide "Fullscreen" on Android; Controls tab text for touch + any gamepad.
  5. App icon in the APK, made automatically for every player (as the PC icon from the save icon, make_icon.py); the
     user's own `.ico` (<home>/Downloads/SonicRidersIcon.ico) as an option (configure/setup `--icon`). Don't commit it
     unless the user says it is their own art.
  6. Analog triggers: RT "felt weak", drift sometimes not full. input_sdl.c sets the digital R/L click only above
     29000 of 32767: log each pad's trigger peak and calibrate the click to ~80-90% of the peak seen.
  Then: GPU cost (specialized shaders from the shader keys, per-draw UBO size), pre-rotation, 2P with two pads.

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

1. Android port (branch `android`): milestone 3, the user's touch/controller feedback next (see "Android" above).
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
