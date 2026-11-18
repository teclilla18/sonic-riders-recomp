# State (2026-11-18)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push. A third Claude session runs on the
user's gaming laptop (online tests). The PC's main checkout folder may be on a local branch: do main work in the
`sr-main` worktree next to it.

## Final Switch 2 continuity

- [Technical handoff](docs/notes/switch2-handoff.md) records architecture, fixes,
  hardware evidence, release validation, rollback points and unresolved issues.
- Shared Codex/Claude workflow is explicit in AGENTS.md. No runtime changes or builds.
- Alpha release body uses the player announcement with old technical details collapsed.
- External install/update Claude Artifacts still need republication and verification.

## Switch 2 Alpha 1 public announcement

- **Released:** v0.8.6-switch2-alpha.1 is a separate experimental pre-release;
  runtime c8c06fdda996 includes scheduler/logging fixes. Known audio/stutter issues remain.
- **Implemented:** docs/art-only main announcement with exact requested text, unchanged
  Lanmana banner/signature and approved SHA-1, alpha install/download/bug-report links.
- Dedicated Switch 2 tabs added to both interactive HTML guides; other platforms and
  stable versions retained. External Claude Artifact pages are prepared for republication,
  not verified updated. No runtime merge/build or stable release changes.
- **Tested:** headless Edge six-tab navigation and click/keyboard checklists pass in
  both guides; exact announcement/banner and original platform panels verified;
  mandatory game-data audit passes [confirmed].
- Other sessions’ dirty sr-main work remains untouched. Announcement uses isolated main
  checkout. Personal Unlimited build remains private and is not part of this release.

## Thursday 2026-11-19 release (user's plan): Switch texture-pack fix, auto stick-spin, iOS

1. **Texture packs without freezes (Switch first)**: background loading. Today texpack_load (texpack.c) runs on the
   game thread inside gx_texture.c's cache miss; on the OLED that froze 65 times (46-114 ms typical, worst 1.3 s,
   7 s total) in one session with a 1 GB BC7 pack. Plan: on a miss, draw the game's own decoded texture now, queue
   the pack file to a low-priority worker (read + bcdec + 512 cap), swap the cache entry's picture when ready
   (gx_frame_retire for the old one). Verify: PC route with `SR_PERFTEST=1` + the pack (no `[hitch]` with
   "lookups"), then the OLED with the full pack (the user copies it over hekate UMS; log on F: or FTP).
   Baseline log: build/old/switch-texpack-20261116.log.
2. **Auto stick-spin option** (user, 2026-11-15: "must be in the NEXT release"; off by default, Gameplay tab; #23;
   notes/plans-2026-11.md "Auto air refill"). Not started. It missed v0.8.5 and v0.8.6 by the user's choice.
3. **iOS zip**: notes and guides promised iOS "this Thursday" (last iOS zip v0.8.4). Built on the Mac from the tag.

## Releases

**v0.8.6 released 2026-11-16** (tag e19f0dd; PC, Switch, Android, Linux zips; cold Sandbox PC + Android PASS).
**HD texture packs in one click**: texpack.c reads DDS (BC1/2/3/7 via bcdec, third_party/bcdec.h), `_arb` names, a
pack with the other `_m` still matches, Switch caps pack textures at 512 (DDS: smaller stored level; PNG halved);
launcher Mods page and Switch installer "Add texture pack..." (Windows tar.exe unpacks zip/7z/rar; GXEE8P folder ->
mods/<name>/textures + mod.ini, on); setup.ps1 copies mods\ to the SD card (robocopy, changed files only).
- PC: **accepted** (the user played it, "I love it"); tested with a 1 GB BC7 Dolphin pack (install 9 s, 1,074
  textures; demo races 59.9 fps, 0 slow frames).
- Switch (user, OLED, mode/clocks not stated, pack copied by hand): **works but freezes** (above); known issue in the
  release notes and README. The installer's SD copy was not run end to end (robocopy step tested on its own).
- Android / iOS packs: built, not tested. That pack (made for a mod) matched 145+ of 738 textures seen on the
  title + two demo races; the rest is expected.
- **Linux zip replaced** the same evening from 6e9e2e7 (#30: zig's error text into setup.log, zig cache in the tools
  folder; tested in WSL, not on a Deck; reply posted on #30, waiting for HazeX2's new setup.log).
**v0.8.5** (tag cf5689a; release deleted when v0.8.6 replaced it): Lv. 5 at 300, drift dash, own screen shape online,
PNG screenshots (seen working on the Switch: shot_114157.png), launcher notes view, RVZ on Mac/Linux, stat-mod syntax.
**Releases page rule:** 5 newest + **v0.8.2 forever** (user). **Easy-read guides** (bumped every release): update
https://claude.ai/artifact/FtNxFJFBdtLqmMFW7QjXFN, install https://claude.ai/artifact/4PSHvwKrxWKTqAp5tDCu6W (iOS rows
point to v0.8.4 until Thursday). iOS test devices signed until **2026-11-21**. Unreleased on main: nothing.
Release lesson: push the tag yourself before `gh release create` (`--verify-tag`); otherwise gh tags remote main.

## Online play (paused for the Thursday items; user chose it 2026-11-15)

Lobby design agreed (docs/ONLINE.md "Lobby design"): room codes, short public list, host's rules on the room card,
punch + relay fallback, no accounts, Discord; Online tab in the overlay; joining = restart + replay the room's input
history ("catch-up"). Nothing of the lobby is implemented.
- [confirmed] PC-Switch and Switch-Switch 2P races (15,000 frames, checksums equal); own screen shape per player
  (protocol 4) tested PC both ways + Switch-Switch, not played by the user.
- The game's 30 fps mode is game state [confirmed, PC]: no per-player frame rate.
- **Next (approved): room-wide 30 fps switch** at an agreed frame; then host rules, stat mods off in sessions, the
  lobby server (notes/online-research.md step 4).

## Other open items, in priority order

1. **Ready-made Android APK in a SEPARATE public repo** (user, 2026-11-16, "absolute next" before the texture work):
   (1) CLAUDE.md rule 1 + check_no_game_data allow exactly this (APK only there, never here, never DX); (2) disc
   picker into main (local branch `android-disc-picker`, 2394600, tested on the S23); (3) optional in-app RVZ
   converter; (4) create the repo + release with notice; link from README. Standing rule: EVERY release advertises
   that repo once it exists (v0.8.5/v0.8.6 could not: it doesn't exist yet).
2. **Hold-B continuous boost** (Riders Plus; user 2026-11-16): holding B keeps boosting, air drained at the rate
   repeated taps would cost (from the gear's boost air cost and length); High Booster (gear 10) keeps one-press.
3. **#29** split-screen HUD shows Lv 3 at Lv. 4/5 (the 1P digits patch, game_levels.c, doesn't cover the
   split-screen ring graphic). Cosmetic; in the v0.8.5/v0.8.6 notes as a known issue.
4. **#23 Ayn Thor slowdown** (Kiri235): GPU-bound busy frames (Adreno 740, same as our S23) [likely our Android
   renderer]; plan: reproduce on the S23 (ask before the heavy run), GPU A/B in render_vk.c, test APK to Kiri235.
5. #30 waits for the reporter; #18 WGP freeze, #12, #24, custom music (#17), right-stick camera, #16.
6. Roster expansion: design approved 2026-11-14 (docs/notes/roster.md), paused.

## Devices

- OLED (switch-remote, 10.0.0.44): `sonicriders.nro` = v0.8.6 build e19f0dd; previous dev build
  `sonicriders.dev-4873a73.nro`; user's `sonicriders.user-20261115.nro`; `mods/HDPackTest/` = the full test pack
  (on in mods.ini: turn it off or remove it for non-texture tests). Clocks handheld 1785 / 614 / 2133, docked
  1785 / 1152 / 2133. When the SD card is in the PC it is F:.
- Second Switch (10.0.0.18, MCP `switch-remote-2`): build 4873a73; sys-clk handheld 1224 / 460 / 1600.
- S23 Ultra: v0.8.1-25 APK, wireless adb 10.0.0.140. Gaming laptop (10.0.0.89): online partner.
- PC `sr-main/mods/` holds the test pack ("super srdx hd texture pack v2 4k dds", on) and old test mods (off).

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] Texture packs with background loading: full pack, a race on 3 stages; no `[hitch]` with "lookups"; memory OK.
- [ ] Switch installer end to end: "Add texture pack..." + BUILD AND INSTALL copies mods/ to the SD card.
- [ ] Room-wide 30 fps switch: OLED + second console, no desync, both switch together.
- [ ] Stock clocks with three swapchain images: fps, no new judder.

## Studies and contacts (2026-11-15)

- ShadowRecomp (GPL-3.0, Xbox Shadow recomp) at build/ext/ShadowRecomp: ideas only for our mod manager.
- BastianWolf80 (YouTube, custom CTR track) invited to Discussions for custom stages.
