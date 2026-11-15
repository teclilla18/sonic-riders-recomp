# State (2026-11-15, late evening)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push. A third Claude session runs on the
user's gaming laptop (online tests). The PC's main checkout folder may be on a local branch: do main work in the
`sr-main` worktree next to it.

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

**v0.8.4 released 2026-11-15** (RVZ/WIA -> ISO, Eject SD card). **Unreleased on main**: zstd RVZ on Mac/Linux, mods
syntax, texture packs, load tracer, Switch PNG screenshots (#10, contributor PR #28: not built for or run on a Switch),
launcher notes view, online protocol 4 (own screen shape), Android INTERNET permission (CHANGELOG "Unreleased").
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

**Must be in the NEXT release (user, 2026-11-15):** the **auto stick-spin** option (off by default, Gameplay tab;
#23; notes/plans-2026-11.md "Auto air refill"). Not started.

1. **Online**: room-wide 30 fps switch (next), then the items in "Current focus".
2. **#23 Ayn Thor slowdown** (Kiri235, logs read 2026-11-15 by log-reader): GPU-bound (Adreno 740 10-14 ms/frame,
   spikes 22) above ~4,000 draws; on "Fit" the 1x EFB is 3416x1056; frame rate was Auto in all runs. Reply posted
   asking for a race in 4:3. If 4:3 helps: cheaper wide EFB on phones; also count speed-blur EFB passes. Two
   0.5-3 s "render wait" hitches (likely the app going to the background) [uncertain].
3. Texture packs on Switch / Android / iOS (memory); update flow first real run at the next release.
4. #18 WGP freeze, #12, #24 (Fedora not confirmed), custom music (#17), right-stick camera, #16.

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] Room-wide 30 fps switch: OLED + second console (its near-stock profile triggers it), no desync, both switch together.
- [ ] A Dolphin-format texture pack in `mods/<Mod>/textures/` on the SD card: replaced, memory OK, no hitches.
- [ ] Switch PNG screenshots (#10): menu "Save screenshot" writes a PNG that opens.
- [ ] Stock clocks with three swapchain images: fps, no new judder.
