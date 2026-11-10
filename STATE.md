# State (2026-11-09)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).

**v0.7.3 released 2026-11-09** (tag on 6db6158): launcher update fix. robocopy skipped same-size, same-time files and every zip used one fixed date, so updating never replaced VERSION (the update was offered again forever). Fix: robocopy /IS /IT /IM, zip entries carry the commit time (so v0.7.1/v0.7.2 launchers update correctly to v0.7.3). Cold Sandbox PC + Android PASS. Zips replaced the same day with entry dates one day before the commit (UTC dates were in the future west of UTC: extra ninja rebuilds). iOS zip: built on the Mac from 6db6158 with the re-dated entries, cold check PASS, uploaded (user approved); all 5 zips on the release. v0.7.2 (Mac launcher) delisted, tag kept. **Accepted** 2026-11-09: the user updated a v0.7.2 Switch installer folder to v0.7.3 with DOWNLOAD AND INSTALL ("worked beautifully").
Windows launcher (+ remapping, fan-art banner kept by the user's choice) and the PC picture options (1x-4x, MSAA,
bloom, colour grading, per-pixel lighting). iOS not in it (needs a Mac setup + `--flavor ios`: notes/ios.md end).
GitHub lists the 5 newest releases (v0.7.0-v0.7.4; v0.7.4 is Latest); on each release delete the 6th (tag kept). OLED and S23 still run v0.5.3 builds.

**iOS, 2026-11-09:** pad fix (A paused races on iPad) accepted on the iPad Pro, on main (ee3aaa1), unreleased;
Stable iOS app IDs implemented (Mac route `<base>.<team>`, plain-ID .ipa so Sideloadly's default reaches the same app; reuse of an installed ID) and tested on the iPad Pro; full setup_mac cold run still to do (HISTORY 2026-11-09 "iOS pads").
**iOS (Mac session; notes/ios.md): verified for v0.7.0** (main 9932634). Races on touch [confirmed, logs]:
iPhone 17 Pro 59.9 fps, iPad Pro 11 M1 59.5, iPad A16 59.3 (first-use shader stutters only), iPad 7th gen ~57
with dips to 40 (A10 GPU ~10 ms/frame). Lock screen 3 min mid-race: no error (game clock held while paused,
`rt_clock_hold`) [confirmed, user]; Control Center pauses game + sound [confirmed, user]; touch controls in
points (iPhone 340 pt, iPad 390 pt), START clear of the Control Center corner [accepted: "controls felt good"].
Release pieces done: `tools/setup_mac.py` ("Install Sonic Riders (iOS).command"), `make_release.py --flavor ios`,
INSTALL/README section, unsigned .ipa without an Apple ID in Xcode (Sideloadly), save-icon app icon, missing /
incomplete disc message. Cold Mac test from the zip PASS (unsigned route); Sideloadly install, message and icon
[confirmed, user]. Not tested: a Mac without Homebrew/Xcode; the signed "install from the Mac" route from the zip.
**Mac launcher for the iOS zip: released in v0.7.2 (current v0.7.3); first start accepted by the user (macOS 27.2)** (notes/ios.md "Mac
launcher"): Sonic Riders Launcher.app (Get ready / Build / Install / Renew / About), custom app icon from any picture,
START HERE (Mac).html. All launchers (PC, Switch/Android installer, Mac) use the user's Super Sonic icon art
(launcher/icon_256.png + icon_32.png; the Mac .icns is generated into build/). Tested by us: a cold Archive Utility extract of the zip (quarantined) keeps 0755 and a valid
ad-hoc signature (Gatekeeper "rejected" = Open Anyway, not "damaged"); full build driven by the launcher from that
folder with a HEIC icon -> .ipa with the cropped icon [confirmed]. First start done by the user on macOS 27.2 (their
screenshots: Done, Open Anyway in Settings, Open Anyway in the second alert, Touch ID, Allow Downloads/Desktop);
START HERE redrawn from them [confirmed, user]. Not tested: the Sideloadly / from-Mac
install pages on a device, a Mac without Xcode/Homebrew. Missing execute bit on the .command: not reproduced
(v0.7.1's zip stores 0755). Building the .ipa on a PC: dropped (user). Every iOS zip is made on the Mac
(`make_release.py --flavor ios --ref <commit>`, not the annotated tag, so VERSION names the commit like the others).
Mac next: the user's free iOS signatures run out ~2026-11-15 (renew); Install/Renew pages with a real device.

**Plan after iOS (user's order), PC only:** (a) internal resolution + anti-aliasing: done on `main` (F1 > Display,
1x-4x, MSAA; HISTORY "2026-11-07, later"), (b) bloom + colour grading, (c) per-pixel character lighting.

**Windows launcher: released since v0.7.0** (notes/launcher.md). Button remapping **accepted**
(2026-11-07, race played); F1 Controls tab shows the live map. Cold Sandbox install PASS. Launcher ICON is settled: the
user's own ChatGPT Super Sonic art, public (notes/launcher.md). Only the BANNER (launcher/banner.jpg, public fan art) still has an
open origin check with its artist (HISTORY 2026-11-07 late; do not ask the user, they will bring it up). Not done: Switch/Android pages. Online play paused at a clean point (section below). On `main`, unreleased: launcher +
remapping, screenshot fix, Android fast-core threads + ADPF (#14), disc-read backstop fix. OLED on v0.5.3 (dev
build on the SD as `sonicriders.dev-20261107.nro`); the S23 has a dev APK.

Consoles: OLED (switch-remote) + two more Switch SD cards have v0.5.3 builds. Clocks: handheld
1785 / 614 / 2133, docked 1785 / 1152 / 2133.

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

- **PC freeze with NVIDIA ShadowPlay recording** (2026-11-08, user): froze loading a race after character select.
  Retest with logging + recording: no freeze, not reproduced [uncertain]. If it recurs: keep the hung process,
  inspect its threads. Workaround given: OBS Game Capture. (HISTORY 2026-11-08 late night)
0. **v0.7.0 released 2026-11-08** (all five zips incl. iOS from the Mac; v0.6.0 delisted). Contents: iOS, PC frame
   rate presets, FSR 1 sharpening/upscaling, controllers in background, Optimus/AMD exports, five logs (all
   **accepted** by the user). GBAtemp post: user posts it (draft given in chat, not in the repo).
   Open checks: F1 sharpness/pad change survives a restart (fix 2207c58, compile-checked only); Optimus on the
   gaming laptop; bug-report zip with five logs; a real pad with another window focused.
   **Next (user's order):** custom music folder (MP3/OGG/WAV/FLAC -> ADX, per-track toggle; Discussion #17), then
   online play, right-stick camera. Other Hookshot ideas left: notes/plans-2026-11.md. Mac not needed unless an
   iOS bug is reported. Android exit crashes fixed (c4cd92a). 32-bit Android: waits for the redditor's phone.
1. **Online play** (above). Release the launcher + PC picture options (cold
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
