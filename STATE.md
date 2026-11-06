# State (2026-11-06, night)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).

**Decisions (user, 2026-11-06):** Android screen recording is not supported; Vita and iOS paused. **v0.5.0 released
2026-11-06; the user is checking it.** Next (user's order after the clear): Steam Deck fixes from the gbatemp report
+ easy issue reporting on Linux/Deck; then online play (below); right-stick camera research; boost effect (#16).

## Steam Deck / Linux (first real-Deck report, gbatemp #19, 2026-11-06, on v0.5.0-linux-test)

- User RonnieD: "It works on steam deck just minor issues like the Full Screen Borderless (can put up with 4:3
  borders for now) and 16:9 Stretch option not working. The chaos board cheat doesn't work as well (not that it
  matters, I plan on unlocking naturally)." Our reply (#20): Chaos Emerald option unlocks it in the shop for 0 rings;
  fixes coming.
- To do: (1) fullscreen on the Deck (Game Mode/gamescope: borderless fullscreen with 4:3 bars; v0.5.0's Fit window
  should fill 16:10; check `SDL_WINDOW_FULLSCREEN_DESKTOP` under gamescope and whether Fit is the right Deck
  default); (2) "16:9 stretch" is gone in v0.5.0 (16:9 widescreen / Fit instead): tell the user; (3) explain the
  Chaos Emerald gear (shop, 0 rings) in the menu text/docs, check it works on Linux; (4) **make reporting issues easy
  on Linux/Deck** (user request): e.g. a menu button / launcher entry that bundles sonicriders.log + setup.log +
  system info into one file in an obvious place and says where, like the Android crash reports; docs link.
- Linux build: `configure.py --toolchain linux`, `make_release.py --flavor linux`; notes/build-and-test.md.

## Ultrawide + FOV (released in v0.5.0; docs/notes/ultrawide-fov.md)

- Modes 4:3, 16:9 (HUD 4:3 middle, option stretch), Fit window (race HUD at the edges, menus middle with
  backgrounds stretched). "16:9 stretched" removed. Mirage and gradient fixed (per-draw 2D anchors).
- Tested: PC det route 16:9/21:9/32:9 + 2P split, menu route + pause at 21:9, Eden, Android emulator. Not seen by
  the user yet; not on Switch hardware or a phone. Unchecked: results screen, story, 3-4P.
- Phones in Fit window: the edge HUD sits under the touch buttons (seen in the emulator): needs a decision.
- PC mouse in the F1 menu (released): click/tabs/sliders tested with posted messages; wheel and the fullscreen
  cursor untested.

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

## Online play (after the Deck fixes)

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

1. Steam Deck fixes + Linux issue reporting (above). Then online play (above).
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
