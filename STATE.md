# State (2026-11-16)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push. A third Claude session runs on the
user's gaming laptop (online tests). The PC's main checkout folder may be on a local branch: do main work in the
`sr-main` worktree next to it.

## 2026-11-16 (PC): README and Ko-fi, no code

README: featured "Riders Plus" section under the intro (banner with the fixed hand, rules table, six 21:9 shots from
the user's recorded PC race replayed at 3440x1440, redrawn charts); top banner cropped to 1529x600; Ko-fi line in
the notice box + Ko-fi badge; repo Sponsor button on (user ticked Sponsorships) [confirmed, repo page]. S23 has the
v0.8.4 release APK (built from the tag) [confirmed, adb install].
Tip for README pictures: a recording made in a window that was 4:3 at start stores the 16:9 camera aspect; patch
the k float at header +20 of the .rec to replay at 21:9 (race played the same), and capture with `SR_SHOTSTEP`
(game frames), not `SR_SCREENSHOT` (presented frames run far ahead in a replay).

## Current focus: online play (user, 2026-11-15; poll #26 ended 1-0 for online, user said "work on online")

User's choices today: (1) slow consoles + **players keep their own settings online** first; (2) lobby: the
recommended design (agreed): room codes (6 chars), a short public room list, host's rules on the room card,
punch first + relay fallback, no accounts, Discord as the meeting place (Kiri235 offered to moderate); (3) phones later.
User's lobby wish: an **Online tab in the in-game overlay** (all platforms) listing internet lobbies; join while the
host is on stage/character select; late join into an open slot at character select. Design answer given (written up
in docs/ONLINE.md "Lobby design"): no save states exist (every guest thread is a host thread), so joining = restart
into the room + replay its input history at full speed ("catch-up"), then the new controller plugs in at an agreed
frame. Long sessions on Switch/phones: the room restarts at the next stage select when someone waits (standings
reset; WGP no late join). Nothing of this is implemented.

- **Tested today [confirmed]** (docs/ONLINE.md, 2026-11-15 entries), 2P race route, 15,000 frames each, all
  checksums equal: **PC-Switch** (OLED host, PC joins; PC 60 fps), **Switch-Switch** over Wi-Fi (OLED host, second
  console joins; 42-60 fps, the second console's sys-clk profile is near stock: 1224/460/1600 handheld).
- **Own screen shape per player** (protocol 4, 4873a73) [implemented, tested]: the room's camera = the wider screen
  (joiner sends its shape in the hello), each machine draws its own shape (gx.vert `GX_XR_ASPECT` narrows the view).
  PC both ways (9,000 / 6,600 frames, joiner's 4:3 picture = offline 4:3 frame) and Switch-Switch (16:9 host, 4:3
  joiner, 12,000 frames). Not played by the user. Older builds can not join this one.
- **The game's 30 fps mode is game state** [confirmed, PC, `SR_DET_SKIP=1`]: drawing at 30 vs 60 with the same inputs
  diverges (racer speed differs by step 10000). So no per-player frame rate and no draw-skipping catch-up. Caveat:
  tested with the mode on from boot, not switched mid-race.
- **Next (approved by the user): the room-wide 30 fps switch** for slow consoles: every machine enters/leaves the
  game's 30 fps mode at the same agreed frame (decided by the host from both machines' lateness, carried in the
  lockstep stream). Then: host rules on the room card; gear-stat mods must be turned off for a session (today a joiner
  with stat mods would desync); the lobby server (notes/online-research.md step 4).
- Kept from before: two PCs over the LAN [confirmed]; launcher lobby (launcher/lobby.cpp) [tested by the user on two
  PCs]; own view full screen [user played it]; online rules (`SR_ONLINE_RULES=1`); Android has the INTERNET
  permission now (2a522a4; APK builds, not run on the phone). Not yet: WGP with the online rules, 3-4 players.

## Launcher release notes (57dc4b7) [implemented, tested PC]

Emoji (Segoe UI Emoji merged, one colour, `IMGUI_USE_WCHAR32`), pictures (JPEG/PNG via WIC, SVG via Direct2D +
DirectWrite text), headings, bullets, clickable links on the Update page; the game's F1 notes drop emoji/pictures.
Screenshots of v0.8.2 notes, the gear chart and live v0.8.4 notes (`SR_LAUNCHER_NOTES=<md>`). Not in a release yet.
Side effect: launcher.cpp, update_core.h, launcher.md, AndroidManifest.xml.in are now LF in the index (were CRLF).

## Roster expansion (paused for online)

Design approved 2026-11-14 night, not started: docs/notes/roster.md "Recommended design" (8 race slots ids 17-24,
base character per custom character, extra 2 x 8 Player Entry pages, portraits via texpack.c). Research and tools
(load tracer `configure.py --trace-loads`) are in notes/roster.md.

## Releases

**v0.8.6 released 2026-11-16 (evening)** (tag e19f0dd; PC, Switch, Android, Linux zips; cold Sandbox PC + Android
PASS; v0.8.5 release deleted right after (tag kept), v0.8.2 is never deleted (user); no iOS zip: notes and guides say
iOS comes Thursday 11-19, iOS points to v0.8.4). New in it: **HD texture packs in one click** (texpack.c DDS BC1/2/3/7
via bcdec, `_arb`, other-`_m` fallback, Switch cap 512; launcher Mods page + Switch installer "Add texture pack...",
Windows tar.exe unpacks zip/7z/rar; setup.ps1 copies mods\ to the SD card with robocopy). **Tested:** PC with a 1 GB
BC7 Dolphin pack (install 9 s, 1,074 textures, demo races 59.9 fps, 0 slow frames); the user played PC ("I love it",
**accepted** on PC). **Not tested:** Switch on hardware (only 15 textures sent by FTP, "very minimum textures load" as
expected; the user is copying the full pack via hekate UMS), the installer's SD copy end to end, Android/iOS packs.
Coverage of that pack on the original game: 145+ of 738 textures on the title + two demo races (pack made for a mod).
Release notes say "largely untested, report issues". v0.8.5 (2026-11-16, tag cf5689a) had the rest of the old
Unreleased list. Linux zip replaced the same evening from 6e9e2e7 (#30: zig error text in setup.log, zig cache in the tools folder;
tested in WSL, not on a Deck; reply posted on #30). Unreleased on main: nothing.
**Easy-read guides** (every release links both): update https://claude.ai/artifact/FtNxFJFBdtLqmMFW7QjXFN, install
https://claude.ai/artifact/4PSHvwKrxWKTqAp5tDCu6W (asked the user whether it is public: no answer yet).
iOS test devices signed until **2026-11-21**.

## Devices

- OLED (switch-remote, 10.0.0.44): build 4873a73 as `sonicriders.nro` (the user's earlier one:
  `sonicriders.user-20261115.nro`); env.txt = the user's; `env.online.txt` = host test env. Clocks handheld
  1785 / 614 / 2133, docked 1785 / 1152 / 2133.
- **Second Switch** (10.0.0.18, MCP `switch-remote-2`, sys-botbase + ftpsrv installed today, HOME tile installed by
  the user): build 4873a73; previous `sonicriders.prev-20261115.nro`; `env.online.txt` = joiner test env. sys-clk
  profile for this title: handheld 1224 / 460 / 1600.
- S23 Ultra: v0.8.1-25 APK, wireless adb 10.0.0.140. Gaming laptop (10.0.0.89): online partner.

## Open items, in priority order

**ABSOLUTE NEXT (user, 2026-11-16): ready-made Android APK in a SEPARATE public repo** (option 3), so players
without a PC can play; a takedown would hit that repo, not this one. Model: Unleashed Recompiled (SEGA; prebuilt
builds on GitHub, players add their own game files). Steps: (1) update CLAUDE.md hard rule 1 + check_no_game_data
to allow exactly this (APK only in the separate repo, never here; no ISO/assets; never DX); (2) bring the disc
picker into main (local branch `android-disc-picker`, 2394600: DiscSetupActivity launcher, tested on the S23:
copy SHA-1 = disc); (3) optional in-app RVZ converter (port tools/rvz2iso.py + zstd); (4) create the repo, release
APK with a notice ("download only here; you need your own Sonic Riders USA disc image"); link it from this README.
Standing rule (user): EVERY release (notes + README) advertises the Android repo with the ready-made app.

**NEXT thing to work on (user, 2026-11-16): hold-B continuous boost** (Riders Plus). Tapping B boosts as now;
**holding B keeps boosting** for as long as it is held, draining air at a fair, calculated rate for the hold time
(derive it from the gear's own boost air cost and boost length, so a hold costs what repeated taps would).
Exception: **High Booster** (gear 10, docs/notes/gear-stats.md; boost costs put it in the red after one boost,
docs/local/gamefaqs-42167-advanced-gear-guide.txt) keeps the one-press boost only. Not started.

**Must be in the NEXT release (user, 2026-11-15):** the **auto stick-spin** option (off by default, Gameplay tab;
#23; notes/plans-2026-11.md "Auto air refill"). Not started.

1. **Online**: room-wide 30 fps switch (next), then the items in "Current focus".
2. **#23 Ayn Thor slowdown** (Kiri235): their 4:3 log (v0.8.2, 1x, Auto) shows CPU fine, GPU 11-17 ms in busy
   frames (max 24), so Auto locks to 30; all shaders specialized, no pipeline hitches; 4:3 changed nothing (not pixel
   cost). Same GPU as our S23 (Adreno 740, driver 802A4035 vs 802A4001) and an old S23 race log (v0.2.4) had similar
   GPU times, so likely our Android renderer near its limit in busy scenes [likely]. User told Kiri235 we would
   reproduce and fix on the S23 and send a test build. Plan: reproduce on the S23 (race with boosts, racers bunched;
   ask before the heavy run), GPU A/B (vertex / fragment shader / the 4 EFB passes per frame with LOAD/STORE in
   render_vk.c efb_begin), make the costly part cheaper, test APK to Kiri235. Logs: 0.zip on #23.
3. Texture packs on Switch / Android / iOS (memory); update flow first real run at the next release.
4. #18 WGP freeze, #12, #24 (Fedora not confirmed), custom music (#17), right-stick camera, #16.

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] Room-wide 30 fps switch: OLED + second console (its near-stock profile triggers it), no desync, both switch together.
- [ ] A Dolphin-format texture pack in `mods/<Mod>/textures/` on the SD card: replaced, memory OK, no hitches.
- [ ] Switch PNG screenshots (#10): menu "Save screenshot" writes a PNG that opens.
- [ ] Stock clocks with three swapchain images: fps, no new judder.

## Studies and contacts (2026-11-15)

- ShadowRecomp (FoxWorkshop, GPL-3.0, Xbox Shadow recomp) cloned at build/ext/ShadowRecomp: ideas only for our mod
  manager (memory: shadowrecomp-study); build it once the user dumps their Xbox copy.
- YouTube: BastianWolf80 (got a CTR track working with Claude) invited to help with custom stages via Discussions; if
  they show up, write a "custom stages: what we know" discussion post.
