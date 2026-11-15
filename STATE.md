# State (2026-11-14, night)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push. A third Claude session runs on the
user's gaming laptop (online tests). The PC's main checkout folder may be on a local branch: do main work in the
`sr-main` worktree next to it.

## Current focus: expanded character roster + modding (user's decision, 2026-11-14 late)

Goal: new characters on their own Player Entry tiles next to the 16 originals (Smash-style, no L/R alternates),
added by community character mods ("anyone can add any character"); a generic mod manager (install, uninstall,
enable, disable; later other mod types). Stages on hold. **Steps 1-2 (research) done; Step 3 design approved by the
user (2026-11-14 night)**: [docs/notes/roster.md](docs/notes/roster.md) "Recommended design" (points 1-5, 2 x 8
pages, page switching L / R + cursor edge, slide animation). Steps 4-5 (mod format, manager) follow Step 3.

- **Research [confirmed, PC]** (docs/notes/roster.md): character ids 0-16; character info table 0x819C4CC0
  (17 x 0x20: type, letter, camera height, stars); racer + 0xBA = character id, and Player Entry edits it directly;
  every file open goes through `fn_80012EA8` (DVDConvertPathToEntrynum); 27 functions read the id in a 1P race;
  id 17 on vanilla = empty panel, no crash. Tournament Edition (rebuilt binary + C++, public GPL source
  RidersBoulevard/sonicriderste: study only) uses ids 17-22, a 3 x 8 grid, and maps each new character to a base
  character's animation set; its gear mask caps it below 32 characters.
- **Injection tests [confirmed, PC, local test mods]**: a new file letter + the mod's files loads and races with no
  code; another character's model on an id crashes in `fn_1_45508` (animation slots are per id) -> each custom
  character must name a base character with the same skeleton.
- **Approved design**: unlimited roster, 8 race slots (ids 17..24 filled per race from the mod data), inherit
  from a base character, files by letter, original 2 x 8 page untouched plus extra 2 x 8 pages of custom
  characters, portraits via texture injection, saves untouched. Not implemented yet.
- **Load tracer** (debug builds: `configure.py --trace-loads`, `SR_TRACE_LOADS*`; notes/debug-variables.md)
  [implemented, used all session]. Character selection in tests: `SR_POKEAT=3520:803E2994:<id>,3520:81A0D7FA:<id>`
  on the 1P route (scripted D-pad/stick for player 1 does not move that cursor; cause unknown).
- **Mods, from SR-Source's ideas** [implemented, tested PC]: `km/h` on any speed, `+=` / `*=`, `[gear.*]`,
  `game = GXEE8P` (other games' mods left out); **Dolphin-format texture packs** (`textures/` in a mod, texpack.c,
  `SR_DUMPTEX_DOLPHIN`): names identical to Dolphin 2609a dumps for 1,507 textures, a 2x test pack replaced the
  boot logos. `_m` (mipmap) names not matched against Dolphin yet (its run saw none). Switch: syntax-checked only.
- README: Decomp Buddy + shields.io badges (Riders and Heroes).

## Online play (paused for the roster, 2026-11-14 late; docs/ONLINE.md, notes/online-research.md)

- Two PCs over the LAN [confirmed: scripted 20,726 frames; user hand sessions 61,811 + 22,477 frames, no desync].
- Launcher lobby (launcher/lobby.cpp) [tested by the user on two PCs]; own view full screen [user played it];
  online rules (netplay.c online_rules, `SR_ONLINE_RULES=1`) [confirmed offline + scripted two machines]: menus by
  P1 only except Player Entry, no pause [not proven], same character allowed.
- Other modes checked offline [confirmed]: Tag, Survival Race/Battle use Player Entry 0x18, P2 joins. Not yet:
  World Grand Prix (Normal Race submenu 0x10), GAME_TOP_MODE in those races, results screens, two machines.
- Laptop helper: serve only exe files over `http://10.0.0.32:8099`, SHA-256; hosts run from Desktop\SR Online Test.

## Releases

**v0.8.3 released 2026-11-14** (tag 76da890; PC, Switch, Android, Linux; iOS stays on v0.8.2). Setup keeps its
build (updates rebuild only what changed) [tested PC + Switch]; one-press UPDATE NOW [auto-build tested; the
download step needs the next release]. Update guide artifact https://claude.ai/artifact/FtNxFJFBdtLqmMFW7QjXFN
(source docs/update-guide.html, republish each release). iOS test devices signed until **2026-11-21**.
Unreleased on main: the mods syntax, texture packs, load tracer (CHANGELOG "Unreleased").

## Devices

- S23 Ultra: v0.8.1-25 APK, wireless adb 10.0.0.140. The Android APK lacks the INTERNET permission (online).
- OLED (switch-remote): c840558 build; clocks handheld 1785 / 614 / 2133, docked 1785 / 1152 / 2133.
- Gaming laptop (GTX 1650 Max-Q, 10.0.0.89): online partner.

## Platforms

- **Windows**: whole game, 1-4P 60 fps, sound, launcher (updates, remapping, Online lobby), picture options, mods.
- **Switch**: 1P ~60 overclocked; 4P steady 30; stock handheld 30 steady.
- **Android / iOS / Linux**: notes/android.md, notes/ios.md.

## Open items, in priority order

0. **Roster Step 3** (approved, not started): build roster.c (8 race slots, table rows from mods), the Player Entry pages, portrait injection, character mod format.
   First tasks inside Step 3: per-character tables in other modes (story, tag, survival, WGP, results, gear shop)
   with the load tracer; the select-screen draw code; gear compatibility (`useFlags`, gear selector).
0b. **Ayn Thor slowdown (#23, Kiri235)**: drops when boosting / racers near. Asked for model, version, frame
   rate mode, 60 vs 30, `sonicriders.log` (waiting). Suspect speed-blur EFB copies on the tiler; plan in
   notes/android.md "Ayn Thor". Worth adding: Android bug-report button, render passes per frame in `[vk]`.
0c. **#24 Linux setup on Nobara** (`cannot find -lstdc++`, no libstdc++-static): fallback to the shared runtime
   (13c7f42) **released** in the v0.8.3 Linux zip (replaced 2026-11-14, from release/v0.8.3-fixes e8456d1). Tested: zip
   setup in WSL with a clang++ that refuses -static-libstdc++ (fallback taken, built, boots 59.9 fps). Not on Fedora;
   Chamlotte told (workaround + new zip); wait for their reply.
1. **Texture packs on Switch / Android / iOS**: build and try a pack (memory use of big HD packs on the Switch).
2. **Update flow, first real run** at the next release (PC launcher + Switch installer via hekate UMS); Linux setup twice.
3. Online: WGP with the online rules, then Switch-Switch / PC-Switch, phones (INTERNET permission), lobby server.
4. Higher frame rates on iOS/Android; in-game update popup (ask first); custom music (#17); right-stick camera; #16.
5. Open checks: F1 sharpness/pad survives a restart; Optimus laptop; bug-report zip; pad with another window focused.

## Wish list / paused

PS4/PS5 on hold; Vita paused (branch `vita`); per-player race sound online (dropped); custom stages (on hold).

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] A Dolphin-format texture pack in `mods/<Mod>/textures/` on the SD card: replaced, memory OK, no hitches.
- [ ] Switch-Switch online session (two consoles, `SR_NET` in env.txt): no desync.
- [ ] PC-Switch session (cross-play): PC host from the test folder, Switch joins.
- [ ] Stock clocks with three swapchain images: fps, no new judder.
- [ ] Copy back `sonicriders.log`, `perftest.json`, `shader_keys.txt`.
