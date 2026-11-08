# State (2026-11-07, late night)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).

**v0.5.3 released 2026-11-07** (tag ef97b2d; Track Select fix in wide modes). v0.5.2 (8db1dc6) made release builds
use specialized shaders (tracked `config/GXEE8P/shader_keys.txt`; OLED handheld 1785/614/2133: 59.9 fps). Only the
latest release is listed on GitHub (older delisted, tags kept). OLED and S23 run v0.5.3.

**iOS: first boot done 2026-11-07** (Mac session; notes/ios.md). iPhone 17 Pro, iOS 27.0, free Apple account:
intro movie + title screen at 60 fps, 2622x1206 (high-DPI), log/screenshots pulled [confirmed, scripted START].
Fixes: UIScene for iOS 27 (`platform/ios/ios_scene.m`), OpenGLES link, high-DPI window, BMP row padding, touch
input (was Android-only), phone menu (no Fullscreen). User: touch works, sharp buttons, Xbox One pad works
[confirmed, user]. Next on iOS: a 1-lap race on touch, background/resume, iPad.
Fan-art app icon via local `--icon` only (never committed).

**Plan after iOS (user's order), PC only:** (a) internal resolution + anti-aliasing: done on `main` (F1 > Display,
1x-6x, MSAA; HISTORY "2026-11-07, later"), (b) bloom + colour grading, (c) per-pixel character lighting.

**Windows launcher: done for the PC zip** (notes/launcher.md), unreleased. Button remapping **accepted**
(2026-11-07, race played); F1 Controls tab shows the live map. Cold Sandbox install PASS. Banner origin to check
(fan art needs the artist's permission; HISTORY 2026-11-07 late). Not done: Switch/Android pages. Online play paused at a clean point (section below). On `main`, unreleased: launcher +
remapping, screenshot fix, Android fast-core threads + ADPF (#14), disc-read backstop fix. OLED on v0.5.3 (dev
build on the SD as `sonicriders.dev-20261107.nro`); the S23 has a dev APK.

Consoles: OLED (switch-remote, 10.0.0.44) + two more Switch SD cards have v0.5.3 builds. Clocks: handheld
1785 / 614 / 2133, docked 1785 / 1152 / 2133.

## v0.5.x contents (released)

- v0.5.2: specialized shaders in release builds (above).
- Ultrawide (notes/ultrawide-fov.md) tested on PC 21:9 Fit window by frames: race HUD at the edges, results,
  pause/quit, Options, Extra, Audio Room, Records, Theater, story intro movie + subtitles (4:3 with bars), 2P.
  Not checked: later in-engine story cutscenes, 3-4P, Tag/Survival/story races. Not accepted by the user yet.
- HOME icon: setup asks y/n, keys at %USERPROFILE%\.switch\prod.keys or a file picker, optional picture [accepted].
- Linux build compiles and runs in WSL (llvmpipe) [not on a Deck]. Race tests: use 1 lap (stage select options).
- Switch "Save screenshot" captured the menu (FPS counter on): fixed on `main` 2026-11-07, tested on the OLED.

Steam Deck / Linux (#19/#20): gamescope fullscreen defaults, Chaos Emerald option, bug-report zip; untested on a Deck.

Plans: docs/notes/plans-2026-11.md (look-back camera, hold-B boost option, icons per device, HD textures).

## Windows host build (the fast test loop)

- Plays the whole game: intro FMV, menus, saves, story mode (cutscenes with voices), 1-4 player races at 60 fps,
  with sound (native AX microcode, `ax.c`, mixed on a worker thread). [confirmed]
- Settings menu: Dear ImGui (F1 / controller Back held 0.5 s); tabs Display, Performance, Controls, Gameplay, Mods,
  About. Frame rate modes: 60 / 30 (full speed) / Auto (default).
- Mods layer 1 (file replacement, `mods/`) and stat mods (`[gear.*]`, `[memory]`, `[gecko]`): working on PC; not
  verified that a race uses changed gear stats.
- `SR_DETCLOCK` determinism: whole-run identical memory every frame on PC, and PC = Switch on the det route.

## Switch build

- Runs the whole game from the Homebrew Menu in title mode, or from the forwarder NSP on the home screen (works on
  hardware, hw16). Sound works; sleep/wake fixed (hw22).
- Graphics: matches Dolphin on hardware (2026-10-29) after the #5/#11 and fog/copy fixes.
- **Overclocked, dev builds** (docked CPU 1785 / GPU 1152 / MEM 2133): 1P races ~60 (perf test: 58.1 fps 1P demo, 56.4 fps 2P demo,
  full speed, game thread 13.5 ms/step); 3P mostly 55-60 with dips to ~48 at 6,000+ draws (hw23/24); 4P a steady
  30 fps (hw25, game core ~17-19 ms of work per frame).
- **Stock clocks, handheld**: 30 fps mode steady; 60 fps mode drops to 30 in 5,000-7,000-draw views (hw33/36).

Last hardware test: 2026-11-07 afternoon (OLED handheld 1785/614/2133, switch-remote): screenshot fix; PC 2P recording replayed (matches PC on all 23847 frames).

## Android port (released in v0.4.0; plan: docs/ANDROID_PLAN.md; history: HISTORY 2026-11-03/04 entries)

- Screen recording: not supported (user, 2026-11-06); device_recover handles one loss (HISTORY 2026-11-06).
- #14 stalls on an Adreno 810 phone: fast-core threads + ADPF hint on `main` (notes/android.md) [untested there].
- Crash reports (v0.4.2) tested on the S23 and the emulator. Reddit "crashes on launch": ask for the report.
- Runs on the S23 Ultra (Android 17): Vulkan renderer, picture = PC on 16 det-route frames, pipeline prewarm,
  background/resume, touch controls, Xbox Series X pad over Bluetooth. [confirmed]
- Perf (S23 Ultra): 1P demo 59.3 fps, 2P 59.8 with specialized shaders; GPU 1P race ~12-13 ms; game ~2 ms/step.
- Scope (user): local split screen up to 2 players with pads; 3-4 players only online (one screen each).
- Touch/controller round 2 accepted (2026-11-03). App icon: the user's .ico via local `--icon` (never committed).
- Next on Android: GPU profiler leads (notes/android.md), pre-rotation, 2P with two pads.

## Wish list / paused

Picture enhancements (resolution scale, bloom, HD textures); edge-anchored HUD for ultrawide; boost effect (top).
PS4/PS5 on hold; Vita paused (branch `vita`, notes/vita-research.md).

## Online play (paused 2026-11-07 at a clean point; docs/ONLINE.md "2026-11-07")

- Done: determinism, record/replay, lockstep with desync check (`SR_NET`), online save, own view, host camera
  aspect, backstops off in sessions, x86 NaN fix, disc-read backstop fix. 2P race route: `tools/net_route_2p.py`.
- **Proven 2026-11-07:** PC recording replayed on the Switch: full RAM equal on 7300 frames (NaN window included),
  checksums equal on all 23847 (menus, 3-lap race, results). Two PC copies over localhost: 35,400 frames in
  lockstep, all equal.
- Next: 1 lap in the route (Track Select "Rule Settings" box: d-pad onto it + A; X/Y/Z do nothing); PC-PC over
  the LAN (laptop), Switch-Switch, PC-Switch with a build from `main`; player docs (host/join, UDP 7777, firewall).
  Local helpers (not tracked): `build/online/run_try.sh`, `run_lan.sh`, `cmphash.py`.

## Open items, in priority order

1. **iOS** play tests (above), then **online play** (above). Release the launcher + PC picture options (cold
   Sandbox PC + Android before any release).
   #14 Android stalls: thread placement + ADPF hint on `main` (S23: no worse, no better; it never stalled). Ask the
   user before posting on #14 (draft: ask for a v0.5.3 log; their v0.4.2 log had 0 fast shaders).
2. **Right-stick camera / look back** (user request 2026-11-06): research how Tournament Edition (or a mod) does
   it; find the race camera's writer; then a C-stick / right-stick mapping. Plans: notes/plans-2026-11.md.
3. Boost camera effect (docs/notes/boost-effect.md, #16).
4. #12 PC at 13 fps: answered (Intel UHD 620). #14: reply posted asking for a v0.5.1 log (Nothing Phone 3a).
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
