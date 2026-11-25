# State (2026-11-25): Private four-player gameplay and standings candidate

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

## Work in progress (branches, not on main)

**Four-player online candidate** (`codex/online-four`, from beta.4). In-game LAN and invite-code rooms
hold 2–4 players. PROTO 6 / NET_COMPAT 8 bundles all inputs through the host. A common visibility
camera keeps game state in sync while each player chooses their screen shape and presentation rate.
The PC test runner defaults to Fit at 1910×800 (2.39:1).

- PC-tested [confirmed]: Metal City, four players on LAN and through the invite-code path on loopback,
  5,400 matching input/checksum records per peer. Mixed local ratios and 30, 45, 60, 120 FPS choices.
  Three-player direct race 5,400 and two-player regression 4,800. The invite-code lobby refused a fifth.
  Disconnecting a guest ended lockstep on all three remaining copies.
- Implemented private four-player race HUD. Each peer schedules the original personal HUD, lap
  panel and gauge for all four controller owners. The renderer composes the selected camera,
  full-frame speed trail and one personal HUD. The map uses its original solo position.
- Tested on four local Windows peers at four aspects. The direct candidate matched 28,200 full
  records, and the LAN candidate matched 23,000, including all four finishes in both races.
  The invite-code iteration matched 21,000. The recovery Windows build (SHA-256 9aaedb7dcb7e)
  matched 20,500 direct-room steps through the same four finishes. Its offline solo and local
  four-player control captures matched their pre-change references byte for byte.
- Recovery point 97543c7. The new private Windows build (4735839df57a, compatibility 7) uses native solo racer distance limits,
  delegates the missing EFB depth test to the renderer, keeps Player Entry models on their cards,
  and replays the same menu-state update as live sessions. Tested 20,500 matching steps through all finishes.
- The subsequent user PC/Switch test exposed extended-level and results issues. The host log matched all 103,340 checksums across three races. Compatibility 8 keeps unfinished racers eligible after the first finish, shares the native standings table, and exposes effective host rules before play. See docs/notes/online-beta.md. PC races matched through the finish in 2P/3P/4P. Windows and Eden matched 20,202 complete records through both finishes, including the Switch guest reaching levels 4 and 5 after the host finished. Physical validation of these changes is pending.

0. **Online Beta 1** (`codex/online-beta`, not released): in-game LAN lobby, saved player names, restart to Online,
   black loading screen, NET_COMPAT check, deterministic disc read-ahead and tester issue form implemented.
   PC-PC finish: all checksums equal through 25,200 frames with cache enabled. Earlier cache-on PC pair through 6,600; PC/OLED racing
   through 17,400 equal (mode/clocks not captured, so no hardware speed claim). S23 restart into lobby tested;
   PC/Android race segment through 6,600 equal on both. Final affinity/clocks menu/cache check passed.
   Separate native candidate: `codex/online-beta-switch2` from public Alpha 3, builds; hardware pending.
   2026-11-21: Windows relaunch keeps an explicit ISO (a5fc6a2, PC-tested); Back to offline PASS on PC; cold Sandbox
   PASS for PC and Android zips (a5fc6a2). Native Switch 2: GPU device loss at game frame 712 blocks it (see
   `codex/online-beta-switch2` STATE.md).
   2026-11-24 beta.4 implemented: NET_COMPAT 4 uses the narrower aspect for both pictures and the game camera.
   Auto spin logs frame and section transitions and checks the movement state before taking the stick.
   PC-tested [confirmed]: Metal City on localhost with 1910×800 and 1424×800 windows, all checksums equal
   through the frame-15,600 checkpoint. Captures show both nearby racers, HUD and rings. The offline
   stale-flag injection preserves manual input, and natural spin sections still work offline and on both peers.
   **Next:** two-home PC beta.4 retest with the ultrawide host and 16:9 joiner. Check HUD, rings, racer
   visibility at distance and steering after a grind. The audio stutter report is still open.
   Every release, issue post or Discussions thread requires explicit OK. Notes: docs/notes/online-beta.md.


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
4. **Online Beta 1 validation** (60 %): finish candidate testing/packages; internet room codes and relay, a shared
   30 fps mode, 3-4 players and ping/delay controls remain later work.
5. **#33 32-bit Android**: branch `claude/android-armv7`; the reporter's APK ran but found no disc image; plain
   copy-the-iso steps posted 2026-11-20. The reporter acknowledged the request on 2026-11-25. The race log is pending.
6. **#6** rare post-race "Now Loading" hang (docs/notes/loading-hang.md). **#29**: fix released in v0.8.7; ask the
   user whether it can be closed. **#31**: Switch Lite update stuck at 0.1.0, no reply yet.
7. **Discussion #34**: user replied. #35: private PC fix tested in local 2P and online 4P Player Entry. Personal review pending.
8. **Switch texture-pack first-appearance freezes**: decode on a worker (texpack.c, gx_texture.c). The user's 4K pack
   caused 113 of 126 stalls on Switch 1 (and audio speed wobble, since everything follows game speed).
9. Super Sonic overhaul (Riders Plus only; plan only). History-only CI failure: ask before changing `--history`.
10. **#37** in-game volume controls, requested and acknowledged on GitHub. **Discussion #36** asks about more netplay players.

## Development device checks

- OLED: the ONLINE application has development build 59f17677537b, NRO and manifest readbacks verified.
  Normal application preserved; previous online build backed up locally. Final menu/cache check: handheld,
  CPU 1785 / GPU 614.4 / MEM 2133 MHz, no worker-core affinity failure. The earlier race used unrecorded clocks.
- S23 Ultra: beta APK cb7ff5ce78a8 installed with readback verification and original APK retained.
  Open Online process restart and PC/Android race segment tested; settings/saves/caches retained.
- Native Switch 2: separate candidate built, not installed or hardware-tested.

## Next hardware test

- Native Switch 2 candidate: name entry, discovery in both directions, loading screen and a cross-platform race.
  Record mode and actual CPU/GPU/memory clocks. Preserve the normal application, settings/saves/caches;
  backup and verify staged/installed NRO and manifest by SHA-256, keep rollback.
- Slow-SD comparison: deterministic read-ahead must keep checksums equal and reduce disc-wait hitches.
- Cold candidate packages need explicit permission before Windows Sandbox; no release/post without OK.
