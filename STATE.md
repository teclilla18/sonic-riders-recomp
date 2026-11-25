# State (2026-11-25)

## Standalone Android installer (local feature branch)

- Phase 1 feasibility documented in [the Android installer roadmap](docs/notes/android-standalone-installer.md).
  Prebuild the disc-free runtime and compile the disc-derived module on the phone.
- Milestone 1 probe implemented and APK verified on the build host. No device test yet. The user will install
  the ARM64 APK from the local branch and check an offline compile, execution, and cached reload on the S23 Ultra.
  Issue #38 tracks the feature.

## Released

- **[v0.9.0 Stable](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0)** (latest, 2026-11-20;
  tag `bce3a5f`): Hold to boost, Auto spin (#23), same character for several players, Super Sonic ring magnet /
  Ring Replenish / 350 km/h held boost, Cheats tab, Switch sleep + start-up clock fixes, unpacked-not-installed
  check. Super Sonic flight is an experimental option, off by default. Zips: PC, Switch, Android, Linux. Cold
  Sandbox PASS: PC (+ space path), Android. iOS zip SonicRiders-iOS-Setup-v0.9.0.zip added to v0.9.0 2026-11-23 (no cold Mac test); guides point to it.
- **[Switch 2 Alpha 3](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0-switch2-alpha.3)**
  (pre-release; tag `67710d8`). Cold Sandbox PASS.
- **Rule (user, 2026-11-20): never delete a GitHub release** (the download totals reset when one is deleted).

## On main since v0.9.0 (docs only)

- README: website announcement line at the very top (b35298f).

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

3. **Online Beta: LAN + internet** (`codex/online-beta` 6bef6d4, pushed, not merged; PC / Switch / Android;
   `codex/online-beta-switch2` be04843 for Switch 2, left out). Internet play **implemented and tested**: invite
   codes (public IPv4, 10 digits / 15 with the port), one UDP socket STUN + lobby + game, host approval, session id,
   ping-based delay, both-codes punching, best-effort UPnP (unproven: nothing answered at home), 10 s STUN keepalive.
   [confirmed] S23 on an iPhone hotspot hosting vs PC (ethernet): equal through 14,400; vs OLED (handheld, CPU 1785 /
   GPU 614.4 / MEM 2133): equal through 26,328; both needed both codes. LAN regression S23 host / OLED join: two
   races equal through 29,741 (user-played). Fixed: player 2 losing control when player 1 finished (per-player
   finish, racer +0xBC) [confirmed]; Super Sonic flight now follows the host online (NET_COMPAT 2) [PC only].
   Known: slow-motion countdown online when a device has texture packs (first-use lookups up to 682 ms on the OLED
   stall the lockstep); posted codes reveal the host's IP (share privately). Notes: docs/notes/online-beta.md.
   Since: no CPU racers online (43110ca, NET_COMPAT 3, PC localhost tested); letter keys type text in menu text
   boxes (ab358f6, built, not tried by typing); launcher Online page points to in-game internet play.
   **First two-home race (2026-11-24, user-played, PC vs PC, beta.3 zip ab358f6)**: connected fine; 2 races, 54,639
   frames, all checksums equal, ping ~55 ms, no network stalls [confirmed by both logs]. Bugs seen: glitchy rings/HUD
   and the other racer vanishing after a short distance (likely: host 2.39 ultrawide vs joiner 1.78, the room camera
   follows the host) [likely]; audio stutter (game-code hitches up to ~300 ms on both PCs) [likely]; Auto spin stuck
   on after a grind until the next auto path ended (no frame numbers in [autospin] lines) [uncertain cause].
   Plan only (needs approval): docs/notes/online-roadmap.md (4P, Super Sonic online rules: no held boost + Ring
   Replenish at half, quick chat, stage voting, rematches).

## Open items (priority order)

1. Online Beta bugs from the two-home race (above): screen-shape mismatch (each player's own shape, or the room
   uses the narrower one), Auto spin after a grind (log frame numbers first, try offline), game-code hitches.
   Also wanted: keyboard number row / numpad in the invite-code box. **#37** (2026-11-24): in-game volume settings;
   the user promised them for the next release.
   Then: background texture-pack loading (time each part of a lookup on the OLED first; isolated branch),
   then beta release prep: website third channel (site.js/build_site.py treat every pre-release as Switch 2),
   online_bug.yml (still LAN-only text), privacy line about codes, Online page; zips, Sandbox, user OK.
   Music packs: launcher page, then the guide (above).
2. Switch 2 into the main release (above).
3. **Super Sonic flight fix** (off-by-default option): after an air trick he stays on his board. SR_FLYLOG=1
   (runtime/src/super_flight.c). Also: iOS v0.9.0 zip from the Mac session.
4. **Switch 2 online GPU crash** (frame ~700, above): rerun the 11-20 pair (S2 normal app 37c357d9510b + OLED
   f0a106b8) to split build vs console, then bisect c572d99..be04843.
5. **#33 32-bit Android** (Redmi A3, PowerVR GE8320): branch `claude/android-armv7` (a06bdc5). The game runs there
   (menus fine), but races show only a small top-left corner of the scene; not reproduced on the S23 (32-bit,
   legacy path forced, reporter's settings). Test 2 posted 2026-11-22: on PowerVR, EFB passes over the whole image
   + colour clears as quads (SR_VK_FULLAREA / SR_VK_QUADCLEAR). Waiting for the reporter's race log. [uncertain]
6. **#6** rare post-race "Now Loading" hang (docs/notes/loading-hang.md). **#29**: fix released in v0.8.7; ask the
   user whether it can be closed. **#31**: Switch Lite update stuck at 0.1.0, no reply yet.
7. **Discussion #34** ("why the GameCube version?"): answered by the user 2026-11-21.
8. **Switch texture-pack first-appearance freezes**: decode on a worker (texpack.c, gx_texture.c). The user's 4K pack
   caused 113 of 126 stalls on Switch 1 (and audio speed wobble, since everything follows game speed).
9. Super Sonic overhaul (Riders Plus only; plan only). History-only CI failure: ask before changing `--history`.

## Devices (user's own)

- **Switch 1 (OLED)**: v0.9.0 + music loader + Music tab (NRO dda4015f..., from claude/music-loader before the format
  commit); v0.9.0 kept as /switch/sonicriders/sonicriders.before-music.nro. Mods: the user's own local music pack (64
  songs, 16 stages) and an HD texture pack shrunk to 512 max. Online app sonicriders-online.nro = beta build
  c4519639 (794a9f8; previous kept as sonicriders-online.before-inet.nro). GXSRTE marker file renamed .png.off.
- **Son's Switch 1** (2026-11-21, by USB mass storage): the OLED's normal NRO (0b2467b8), Online app c4519639,
  /NSP/SonicRidersOnline.nsp (to install with DBI/Goldleaf), mods GXSRTE (marker fix) + astro waterfall +
  super-srdx-hd-textures; his old NRO kept as sonicriders.before-20261121.nro. SD nearly full (2.5 GB free).
- **S23**: online beta APK 9503d679 (794a9f8); backup of the previous one in .local/online-beta/build/old/phone-backup/.
- **Switch 2**: Alpha 3 + music loader + Music tab (NRO cf00d5dc..., local branch claude/s2-music, never pushed);
  Alpha 3 kept as sonicriders-switch2.nro.before-music on the console and in .local/s2-music/build/mtp/. Same mods.
  Its FTP server (OunceFTP, 10.0.0.114:1337) needs cwd + short names (full paths with spaces fail) and once crashed
  on a folder create by full path; cwd + MKD name worked.

## Next hardware test

- Online PC vs PC (user + brother): both at the same screen shape (4:3 or 16:9): do rings/HUD and the other racer
  look right? Grind with Auto spin on: does it stay stuck? Both send sonicriders.log.

- Online (OLED + S23 or the son's Switch, LAN and internet): a race with Super Sonic flight on at the host; CPU / 1P
  markers visible with GXSRTE (marker file renamed); host waits 3+ min before the join (keepalive); state mode +
  clocks. Texture packs on vs off: countdown speed.
- Switch 2: play the music build (Music tab, D-pad picks) and the 512 texture pack; report whether the music speed
  wobble is gone on both consoles (docked CPU 1785 / GPU 1152 / MEM 2133; handheld GPU 614; state the mode).
- An MP3 or OGG song in a pack on a Switch: conversion time at the race's loading.
