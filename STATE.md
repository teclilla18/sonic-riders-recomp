# State (2026-11-07)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).

**v0.5.0 was pulled (deleted) by the user on 2026-11-07 for its ultrawide bugs.** Fixes are on `main`, not
released. Next session (user's order): check GitHub issues and Discussions first; then the ultrawide checks
below; then the user tests the HOME-screen icon; then release **v0.5.1** (user, 2026-11-07: keep the v0.5.0 tag;
v0.5.0-linux-test pre-release is still up with the old bugs; cold sandbox tests first).

## Top priority: ultrawide fixes + Switch HOME-screen icon (both on `main`, unreleased)

- Ultrawide (notes/ultrawide-fov.md): edge HUD only while racing (`GAME_MENU_FLAG` 0x8021C1AC), letters of a
  line anchored together, menu bars reach the edges, screenshots without the F1 menu. Checked on PC 21:9 det
  routes (race, pause, quit dialog, Mode Select, Normal Race, Extras, Gear Gallery). **Still to check:** story
  cutscenes and Theater story movies (the user's subtitle case: needs a story save), Options, Records, Audio
  Room, results screen, 16:9 and 32:9 again, 2P split; 1-pixel seams in menu bars (user's shots).
  The user's v0.5.0 screenshots: build/uw2/user/; test routes: build/uw2/route_*.txt (SR_DETCLOCK=1).
- HOME icon: setup.ps1 asks after "What to build" (prod.keys + optional picture) -> tools/nsp/make_forwarder.py
  -> `<sd>\NSP\SonicRiders-HOME-icon.nsp`, install with DBI/Goldleaf (docs/INSTALL.md). Built here with the
  user's keys and icon: build/nsp/test/SonicRiders.nsp (NPDM identical to the hw16 one that worked). **Needs:**
  the user to install it on the console; one end-to-end setup run with the question (not run yet).
- User's side question answered: v0.4.2's setup never made a forwarder (only a manual script); now it does.

## Steam Deck / Linux (gbatemp #19/#20, on v0.5.0-linux-test)

- Done on `main`, untested on a Deck: under gamescope / SteamDeck=1 the window opens at the screen size and a
  first run defaults to fullscreen + Fit window; Chaos Emerald option off by default with clear text (Shop, 0
  rings); "Save a bug report" zip (F1 > About; tested on Windows; Linux build not compiled this session).
- Reply to RonnieD drafted in the session summary (16:9 stretch removed -> 16:9 widescreen / Fit window).

## Plans (docs/notes/plans-2026-11.md)

Look-back camera (no public code found; plans A render-only / B camera writer), nitrous-style hold-B boost
option, a windowed launcher (SwitchWakerHD's Avalonia installer studied; MPL-2.0), building on Linux/Deck and
Android, custom icons per device (PC/Android/Switch HOME yes; .nro and Linux not yet).

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
- Graphics: matches Dolphin on hardware (2026-10-29) after the #5/#11 and fog/copy fixes.
- **Overclocked** (CPU 1785 / GPU 1152 / MEM 2133): 1P races ~60 (perf test: 58.1 fps 1P demo, 56.4 fps 2P demo,
  full speed, game thread 13.5 ms/step); 3P mostly 55-60 with dips to ~48 at 6,000+ draws (hw23/24); 4P a steady
  30 fps (hw25, game core ~17-19 ms of work per frame).
- **Stock clocks, handheld (384 MHz GPU profile)**: 30 fps mode is a steady 30 at full speed in every race block;
  60 fps mode drops to 30 in views with 5,000-7,000 draws because the game thread needs 15-21 ms per frame
  (hw33, hw36). GPU 7-13 ms per race frame after the shader-key fix (v0.3.2).
- Specialized shaders cover every state (hw36: 2,563,268 of 2,563,268) once keys ignore unused bits.

Last hardware test: 2026-10-30 (overclocked, switch-remote), see Online; `perf/results/2026-10-30-switch.json`.

## Android port (released in v0.4.0; plan: docs/ANDROID_PLAN.md; history: HISTORY 2026-11-03/04 entries)

- Screen recording: not supported (user decision 2026-11-06). On `main` (not released): the renderer rebuilds itself
  after a single device loss (`device_recover`) [confirmed]; under `screenrecord` losses repeat and it still ends in
  the fatal error. Details: HISTORY 2026-11-06, docs/notes/android.md "Open: device lost".
- #14 music stops on an Adreno 810 phone: asked for the attached log, model and a 30 fps test (lead: 70-150 ms
  game-code hitches starving the CRI stream thread) [uncertain].
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
- Next on Android: GPU profiler leads (notes/android.md), pre-rotation, 2P with two pads.

## Wish list / paused

Picture enhancements (resolution scale, bloom, HD textures); edge-anchored HUD for ultrawide; boost effect (top).
PS4/PS5 on hold; Vita (branch `vita`, notes/vita-research.md) and iOS paused by the user.

## Online play (not worked on 2026-11-07; after the release)

Where it stands (docs/ONLINE.md "Phase 3" + "2026-11-06"):
- Done before: determinism, record/replay, lockstep with desync check (`SR_NET`), online save, own view. PC-Switch
  session 2026-10-30 identical to frame 6917, then a desync from a NaN sign (x86 vs ARM).
- **Implemented 2026-11-06, in v0.5.0, untested between machines:** host's camera aspect for everyone (protocol 3,
  recordings too); backstops off during sessions; x86 NaN creation -> +NaN (ppc_nan2/3 in recompiled float ops).
- Next: rebuild the 2P race route script (scratchpad lost; online save, SR_PADS=2, player 2 joins at Player Entry,
  Knuckles, extra A for the course, START ~4700, stick forward SY=100 + SX steering); record it on PC and replay
  in Eden (ARM): frames 6918-7103 must match (NaN fix proof); two PC copies over localhost (`SR_NET=host:127.0.0.1:
  7777` / `join:127.0.0.1:7777`); player setup docs (how to host/join, ports, firewall); then the user's
  PC-PC / Switch-Switch / PC-Switch hardware tests.

## Open items, in priority order

1. Issues/Discussions, ultrawide checks, HOME icon test, release. Then online play (above).
2. **Right-stick camera control / look back** (user request 2026-11-06): research. The user recalls Sonic Riders
   Tournament Edition (or another remake/mod) lets you look backwards; find how it does it (Gecko codes, TE source
   or docs), and find the race camera's writer (earlier attempt: "camera writer not found"). Then a C-stick/right
   stick mapping (Android right stick already maps to the C-stick).
3. Boost camera effect (docs/notes/boost-effect.md, #16).
4. #12 PC at 13 fps: answered (Intel UHD 620). #14 Android music: waiting.
5. #1 stock-clock performance; #2 3-4 player races at 30 fps; audio crackle at stock clocks (logs needed).
6. #3 speed blur weaker than GameCube; #6 rare "Now Loading" hang (unproven fixed); #4 story monitor; #7 lens flare.
7. Small graphics, Switch `[applet]` log, single Joy-Con, mods on Switch, #9/#10, PGO etc. (see HISTORY).

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] HOME-screen icon: install build/nsp/test/SonicRiders.nsp (or the setup's) with DBI; open it; quit the game.
- [ ] 16:9 wide mode (HUD 4:3) and FOV 120 % in a race: picture, fps vs normal 16:9 (EFB 854 x 2 columns).
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
