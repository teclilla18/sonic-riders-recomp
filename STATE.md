# State (2026-11-21)

## Released

- **[v0.9.0 Stable](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0)** (latest, 2026-11-20;
  tag `bce3a5f`): Hold to boost, Auto spin (#23), same character for several players, Super Sonic ring magnet /
  Ring Replenish / 350 km/h held boost, Cheats tab, Switch sleep + start-up clock fixes, unpacked-not-installed
  check. Super Sonic flight is an experimental option, off by default. Zips: PC, Switch, Android, Linux. Cold
  Sandbox PASS: PC (+ space path), Android. iOS v0.9.0 not built yet (needs the Mac); guides point iOS to v0.8.7.
- **[Switch 2 Alpha 3](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0-switch2-alpha.3)**
  (pre-release; tag `67710d8`). Cold Sandbox PASS.
- **Rule (user, 2026-11-20): never delete a GitHub release** (the download totals reset when one is deleted).

## On main since v0.9.0 (docs only)

- docs/TEXTURE-PACKS.md: texture packs on every device with nine drawn pictures; linked from the README (Get it
  line, texture details) and MODDING.md. Switch 2 section: copy by hand (its installer has no pack button yet).

- Website (gh-pages fa21b1d, 98f33f0, 2026-11-21): v0.9.0 features on the Riders Plus page (Hold to boost, Auto
  spin, same character, Super Sonic 350 km/h / magnet / Ring Replenish / flight option, toggles, Cheats tab, release
  charts), home ticker/teaser, FAQ (texture guide link, sleep fix, FINISH UPDATE). Checked in Chrome at desktop and
  phone width. Rule (user, 2026-11-21): update the website after every release, pleasing and easy to read.

## Work in progress (branches, not on main)

1. **Music packs** (`claude/music-loader`, worktree `.local/music-loader`, pushed): own songs per stage
   (`mods/<pack>/music/<stage>/`, MP3/OGG/FLAC/WAV/ADX), shuffle per race, D-pad hold picks song 1-4, Music tab in
   the settings menu, `music.ini` choices. Notes: docs/notes/music.md (on the branch).
   - Tested: PC routes; **user-played on Switch 1: shuffle and all four D-pad picks work** [confirmed]. Format
     conversion tested on PC only. The guard's file limit is 1 MB on this branch (user's choice, for dr_flac.h).
   - **Next: the PC launcher Music page** (one focused piece; design = the mockup
     https://claude.ai/artifact/Xd8GembqJmoXYtM55yVLER), **then docs/MUSIC.md** (player guide with pictures), then
     merge to main and release. Menu-music categories (S20...) and naming the D-pad directions are open.
2. **Switch 2 into main** (`claude/switch2-mainline`, worktree `.local/switch2-mainline`, pushed WIP 8330af0):
   main + Switch 2-only files + three-way merge of render_vk.c / launcher.cpp / sandbox_test.py (Alpha 3 had lost
   main's #32, installer_layout and unpacked-not-installed fixes). Installer: UPDATE NOW for Switch 2
   (`SonicRiders-Switch2-Setup-<version>.zip` in each release), texture-pack button, no "Alpha". All four targets
   build; nothing run on a device yet. Next: PC launcher check, Switch 2 hardware run (log `[vk] ... dynamic`),
   README/booklets/guides, then one release with the user's OK and Sandbox tests (PC, Android, Switch 2).

## Open items (priority order)

1. Music packs: launcher page, then the guide (above).
2. Switch 2 into the main release (above).
3. **Super Sonic flight fix** (off-by-default option): after an air trick he stays on his board. SR_FLYLOG=1
   (runtime/src/super_flight.c). Also: iOS v0.9.0 zip from the Mac session.
4. **Online play** (Road to v1.0 area, 55 %): internet play with room codes, hole punching + relay
   (docs/ONLINE.md "Lobby design", docs/notes/online-research.md), Online tab in the in-game overlay.
5. **#33 32-bit Android**: branch `claude/android-armv7`; the reporter's APK ran but found no disc image; plain
   copy-the-iso steps posted 2026-11-20, waiting. Code fix idea: on-screen message, accept an .iso in files/.
6. **#6** rare post-race "Now Loading" hang (docs/notes/loading-hang.md). **#29**: fix released in v0.8.7; ask the
   user whether it can be closed. **#31**: Switch Lite update stuck at 0.1.0, no reply yet.
7. **Discussion #34** ("why the GameCube version?"): answered by the user 2026-11-21.
8. **Switch texture-pack first-appearance freezes**: decode on a worker (texpack.c, gx_texture.c). The user's 4K pack
   caused 113 of 126 stalls on Switch 1 (and audio speed wobble, since everything follows game speed).
9. Super Sonic overhaul (Riders Plus only; plan only). History-only CI failure: ask before changing `--history`.

## Devices (user's own)

- **Switch 1 (OLED)**: v0.9.0 + music loader + Music tab (NRO dda4015f..., from claude/music-loader before the format
  commit); v0.9.0 kept as /switch/sonicriders/sonicriders.before-music.nro. Mods: the user's own local music pack (64
  songs, 16 stages) and an HD texture pack shrunk to 512 max.
- **Switch 2**: Alpha 3 + music loader + Music tab (NRO cf00d5dc..., local branch claude/s2-music, never pushed);
  Alpha 3 kept as sonicriders-switch2.nro.before-music on the console and in .local/s2-music/build/mtp/. Same mods.
  Its FTP server (OunceFTP, 10.0.0.114:1337) needs cwd + short names (full paths with spaces fail) and once crashed
  on a folder create by full path; cwd + MKD name worked.

## Next hardware test

- Switch 2: play the music build (Music tab, D-pad picks) and the 512 texture pack; report whether the music speed
  wobble is gone on both consoles (docked CPU 1785 / GPU 1152 / MEM 2133; handheld GPU 614; state the mode).
- An MP3 or OGG song in a pack on a Switch: conversion time at the race's loading.
