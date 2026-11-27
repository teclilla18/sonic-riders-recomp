# State (2026-11-26, late night)

## Released

- **[v0.10.2](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.10.2)** (latest, 2026-11-26 late night):
  Switch Open Online crash fixed: the in-game restart is impossible on Switch (the loader maps the next NRO in the same
  process), so online has its own app sonicriders-online.nro ("Sonic Riders Online", Online icon) in
  /switch/sonicriders-online/, plus an Online HOME icon (make_forwarder.py --online, 010053520d9b0000). OLED handheld
  (CPU 1785 / GPU 614 / MEM 2133) vs PC v0.10.1 LAN: 15,600 checksums equal [confirmed]; user ran the update on a second
  Switch 1 with both HOME icons [confirmed, user]. #42: ARM Mali uses legacy Vulkan render passes, crash reports name the
  renderer step (S23 validation 0 errors both paths; no Mali device) [likely]. Launcher banner restored. No-PC app v0.7
  (code 7). Cold Sandbox PASS PC (+ space) and Android. NET_COMPAT unchanged (races v0.10.0 / v0.10.1).
- **[v0.10.1](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.10.1)** (2026-11-26; tag `30ba4c3`):
  #42 fix. Since v0.10.0 every frame's end-of-frame present ran as an EFB copy into texture 0 (0x0 area); after the
  texture cache freed texture 0 (often at race start) frames wrote into a destroyed image, and Mali drivers crashed.
  Now only the pre-boot Online screen copies (copy[4] set), in render_vk.c / render_gl.c / render_deko.c. Tested: S23
  Vulkan validation 15 -> 0 errors, PC scripted demo, Switch built only; **not run on a Mali phone** [likely fixed].
  Cold Sandbox PASS (PC + space path, Android). Android no-PC app v0.6 (code 6, same signer) carries the fixed
  runtime.o: S23 update over v0.2 kept data, Build update on the phone, scripted 1P/2P races ran [confirmed on S23].
  NET_COMPAT unchanged: v0.10.0 and v0.10.1 race each other.
  **#42 NOT fixed for the reporter** (comment 2026-11-26 night): same crash at race / story start on v0.10.1 itself
  (no-PC app 0.6, build 30ba4c3e), Infinix X6886, Mali-G57 MC2, Android 16: SIGSEGV address 0x64 inside
  libGLES_mali.so (+8FB1A0). Log kept locally in .local/issue42-logs/ [confirmed: the fix did not cover this phone].
- **[v0.10.0 Online Beta](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.10.0)** (2026-11-26;
  tag `ecdd995`): two-player online crossplay (PC, Switch family, 64-bit Android; LAN + internet invite codes; Race Room
  live chat on the website), balanced Super Sonic + Limit Break option, Android without a PC (beta,
  `RidersRebuilt-Android-NoPC-v0.10.0.zip`), Riders Rebuilt name, RR launcher icon. Cold Sandbox PASS (PC + space path,
  Android) on d3a83ee; the later commits only change the launcher icon and docs. Linux / iOS stay on v0.9.0.
  Four-player online: next update (in testing on codex/online-four).
- **[v0.9.0 Stable](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0)** (2026-11-20;
  tag `bce3a5f`): Hold to boost, Auto spin (#23), same character for several players, Super Sonic ring magnet /
  Ring Replenish / 350 km/h held boost, Cheats tab, Switch sleep + start-up clock fixes, unpacked-not-installed
  check. Super Sonic flight is an experimental option, off by default. Zips: PC, Switch, Android, Linux. Cold
  Sandbox PASS: PC (+ space path), Android. iOS zip SonicRiders-iOS-Setup-v0.9.0.zip added to v0.9.0 2026-11-23 (no cold Mac test); guides point to it.
- **[Switch 2 Alpha 3](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0-switch2-alpha.3)**
  (pre-release; tag `67710d8`). Cold Sandbox PASS.
- **Rule (user, 2026-11-20): never delete a GitHub release** (the download totals reset when one is deleted).

## On main since v0.10.2

- Race Room typing chat (website) is live: server/race-room/ (Cloudflare Worker riders-rebuilt-race-room); deploy steps in
  its README.md; Game Dev links/media accepted live, video player untested.

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

1. **#42**: v0.10.2 moves Mali to legacy render passes (unproven on Mali). Next: the reporter's result; if it still
   crashes, the new crash.txt names the Vulkan step and thread. #43 closed as duplicate.
2. **Four-player online update** (promised in every v0.10.0 text as "coming soon"): `codex/online-four` (compat 8,
   private) has 3-4P rooms; v0.10.0 limits rooms to 2 (LAN_ROOM_LIMIT on `claude/online-balance`). The 3P test showed
   each device drawing every split view (blurry HUD, S23 GPU-bound ~53 fps): one screen per player first.
   Also promised for the next release: **#37** in-game volume sliders. Follow-ups: #39 Android cutscenes after sleep,
   first-use texture-pack stalls online, #38 can be answered/closed (no-PC Android shipped in v0.10.0; user's call).
3. Switch 2 into the main release (above).
4. **Super Sonic flight fix** (off-by-default option): after an air trick he stays on his board. SR_FLYLOG=1
   (runtime/src/super_flight.c). Also: iOS v0.9.0 zip from the Mac session.
5. **Switch 2 online GPU crash** (frame ~700, above): rerun the 11-20 pair (S2 normal app 37c357d9510b + OLED
   f0a106b8) to split build vs console, then bisect c572d99..be04843.
6. **#33 32-bit Android** (Redmi A3, PowerVR GE8320): branch `claude/android-armv7` (a06bdc5). The game runs there
   (menus fine), but races show only a small top-left corner of the scene; not reproduced on the S23 (32-bit,
   legacy path forced, reporter's settings). Test 2 posted 2026-11-22: on PowerVR, EFB passes over the whole image
   + colour clears as quads (SR_VK_FULLAREA / SR_VK_QUADCLEAR). Waiting for the reporter's race log. [uncertain]
7. **#6** rare post-race "Now Loading" hang (docs/notes/loading-hang.md). **#29**: fix released in v0.8.7; ask the
   user whether it can be closed. **#31**: Switch Lite update stuck at 0.1.0, no reply yet.
8. **Discussion #34** ("why the GameCube version?"): answered by the user 2026-11-21.
9. **Switch texture-pack first-appearance freezes**: decode on a worker (texpack.c, gx_texture.c). The user's 4K pack
   caused 113 of 126 stalls on Switch 1 (and audio speed wobble, since everything follows game speed).
10. Super Sonic overhaul (Riders Rebuilt only; plan only). History-only CI failure: ask before changing `--history`.

## Devices (user's own)

- **Switch 1 (OLED)**: v0.10.2 test builds from claude/issue42-mali: /switch/sonicriders/sonicriders.nro (bdaf608a) and
  /switch/sonicriders-online/sonicriders-online.nro (cb6713de, Online icon); the user's music-pack build kept as
  sonicriders.music-v090-before-v0101.nro; old builds/shots in /switch/sonicriders/_delete-me/. Online HOME icon NSP
  in /switch/sonicriders-online/ (install with DBI). sys-clk: profiles for 010053520D9A0000 and 010053520D9B0000 added
  2026-11-26 (handheld 1785/614/2133, docked 1785/1152/2133); before that Riders ran at stock clocks from HOME icons.
- **Second Switch 1**: updated to v0.10.2 with the release installer (both HOME icons installed, user-confirmed);
  leftovers in /switch/sonicriders/_delete-me/. Runs stock clocks (no sys-clk profile; user's call).
- **S23**: no-PC app net.srnative.sonicriders.standalone v0.7 (code 7, v0.10.2 engine), updated over v0.6 with data
  kept; on-phone Build update started 2026-11-26 (result not checked). Main app: v0.9.0-25 dev APK (backup
  .local/issue42-mali/build/old/phone-backup/before-issue42-mali.apk); a debuggable validation APK was installed
  over it for #42 tests and is still there (GPU debug-layer settings removed after the tests).
- **Switch 2**: Alpha 3 + music loader + Music tab (NRO cf00d5dc..., local branch claude/s2-music, never pushed);
  Alpha 3 kept as sonicriders-switch2.nro.before-music on the console and in .local/s2-music/build/mtp/. Same mods.
  Its FTP server (OunceFTP, 10.0.0.114:1337) needs cwd + short names (full paths with spaces fail) and once crashed
  on a folder create by full path; cwd + MKD name worked.

## Next hardware test

- OLED Online race again now that sys-clk covers the Riders title IDs: state mode + clocks from the log's
  [hardware] line; the bad race (stock 1020/307/1331, host) fell to ~28 fps at 8,000+ draws/frame late in the
  stage, checksums all equal.
- #42 reporter (Mali-G57) on no-PC v0.7: race start; if it crashes, crash.txt now names the Vulkan step.
- Online PC vs PC (user + brother): same screen shape; rings/HUD and the other racer; Auto spin on a grind.
- Switch 2: music build + 512 texture pack (music speed wobble), state the mode and clocks.
