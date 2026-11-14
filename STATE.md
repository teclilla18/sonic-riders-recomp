# State (2026-11-14, late night)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push. A third Claude session runs on the
user's gaming laptop (online tests, below).

## Current focus: online play and cross-play until v1 (user's decision, 2026-11-14)

Plan and research: docs/notes/online-research.md (lobby codes, hole punching, relay server; 3-4 players through the
host; adjustable delay 3-15 frames from the measured ping, as the SRTE community plays). Mechanics: docs/ONLINE.md.

- **Two PCs over the LAN** [confirmed]: scripted 20,726 frames (desktop + laptop, .bat files); the user played two
  hand sessions through the launcher lobby: 61,811 and 22,477 frames, every checksum equal, no desync. A scripted
  run with the online rules: 8,966 frames equal, recordings byte-identical.
- **Launcher lobby** (Online page, launcher/lobby.cpp) [tested by the user on two PCs]: name (no default), Host
  (join code = the host's LAN IPv4 in 7 characters), list of LAN lobbies, join by code, players' names, START RACE
  starts the game on both. Two players. Fixed after the user's test: the game started again by itself after a
  race (black fullscreen), the bottom PLAY button in a lobby.
- **Own view full screen** (gx_render.c ownview_frame) [confirmed PC frames; user played it]: this player's half,
  3D with more above/below (user picked it over zoom), HUD to the corners, whole map, timer for both, speed blur.
  The HUD goes to the screen edges whenever the session picture is wide (joiner set to 4:3 kept it in the middle,
  fixed 2026-11-14, not seen by the user yet).
- **Online rules** (netplay.c online_rules; header flag 8; `SR_ONLINE_RULES=1` offline) [confirmed offline + two
  machines scripted; partly played]: sessions open at the main menu (frame ~780) by themselves; menus by player 1
  only, except Player Entry (characters/boards; an unjoined player cannot back out; only P1's START); no pause in
  races [not proven: a scripted START never paused even without the rule]; after the finish only P1 is heard;
  several players may pick the same character (taken table cleared) [confirmed offline: two Sonics race].
- **Screen options**: only "4:3 (original)" and "Fit to screen" (game menu + launcher); old 16:9 settings read as Fit.
- **Dropped by the user (2026-11-14)**: each player hearing only their own racer (sounds carry no player; would need
  a tag from racer update to AX voice in our mixer; findings in HISTORY 2026-11-14).
- **Laptop helper session** (Remote Control peer, name changes; ListAgents): fetches builds from the PC over the LAN
  (`python -m http.server 8099 --bind 10.0.0.32` serving ONLY exe files from a scratch folder, SHA-256 checked),
  installs into its Desktop\SR Online Test (launcher layout + baserom copy), runs scripted joiners, reports `[net]`
  lines. PC test folder: Desktop\SR Online Test (same layout). A game started from another path is blocked by the
  PC firewall: run hosts from the test folder's exe. With stderr redirected the game writes no sonicriders.log.

## Releases

**v0.8.2 released 2026-11-14** ("Sonic Riders Plus", tag on 81ee335; all five zips; iOS zip rebuilt from the tag +
iOS renew fix; cold Sandbox PC + Android and cold Mac PASS). Riders Plus details: HISTORY 2026-11-13/14 and
notes/game-addresses.md (charge-jump grace 0.75 s [accepted, PC]; Lv. 4/5 at 200/350; gold capsule respawn; dash
panels / Speed Shoes). The online work (everything above) is on `main`, unreleased (CHANGELOG "Unreleased").
iOS test devices signed until **2026-11-21** (renew before then). GitHub lists the 5 newest releases.

## Devices

- S23 Ultra: v0.8.1-25 APK, wireless adb 10.0.0.140. **The Android APK lacks the INTERNET permission** (online).
- OLED (switch-remote): c840558 build (three swapchain images), settings 1.5x / Fit / Auto / bloom 70; clocks
  handheld 1785 / 614 / 2133, docked 1785 / 1152 / 2133. Two more Switch SD cards on v0.5.3.
- Gaming laptop (GTX 1650 Max-Q, 10.0.0.89): online partner; Python 3.12 installed (user-approved).

## Platforms

- **Windows**: whole game, 1-4P 60 fps, sound, launcher (updates, remapping, Online lobby), picture options.
- **Switch**: 1P ~60 overclocked; 4P steady 30; stock handheld 30 steady. Online: sockets work (2026-10-30 LAN
  session); the lobby is PC-only so far (Switch needs SR_NET in env.txt).
- **Android / iOS / Linux**: notes/android.md, notes/ios.md; online not tried there.

## Open items, in priority order

1. **Online: other modes** (user, next). Checked 2026-11-14 [confirmed, PC offline, `SR_ONLINE_RULES=1 SR_PADS=2`,
   scripted, frames]: Tag Mode (0x0F -> 0x13 -> 0x16 -> 0x18), Survival Race and Survival Battle (0x0F -> 0x14
   submenu -> 0x16 -> 0x18) use the same Player Entry 0x18: player 2 joins, the race starts; same character seen in
   Survival Battle (both Sonic). Not yet: World Grand Prix (Normal Race submenu 0x10; screens between races must
   not wait on player 2), whether GAME_TOP_MODE is 8 in those modes' races (no-pause rule), their results screens,
   two machines (laptop session). Offline helper scripts were in the session scratchpad (lost): rebuild from
   tools/net_route_2p.py.
2. Online, for the user's next hand test: no pause in races, HUD at the edges for a 4:3 joiner, same character,
   main-menu start, only P1 starts the race / runs the results.
3. Online next steps (notes/online-research.md "Steps"): Switch-Switch and PC-Switch sessions; phones (INTERNET
   permission); lobby server with codes + punch + relay (ask the user who hosts it); 3-4 players; ping + delay
   setting in the lobby.
4. Higher frame rates on iOS/Android (render_vk.c pc_fps); MSAA / smooth lighting on Switch/Android/iOS.
5. In-game update popup (plan first, ask the user); custom music folder (#17); right-stick camera; boost effect (#16).
6. Open checks: F1 sharpness/pad change survives a restart; Optimus on the gaming laptop; bug-report zip; a real
   pad with another window focused. Older issues #1 #2 #3 #4 #6 #7 #12 #14 (ask before posting on #14).

## Wish list / paused

HD textures; PS4/PS5 on hold; Vita paused (branch `vita`); per-player race sound online (dropped for now).

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] Switch-Switch online session (two consoles, `SR_NET` in env.txt, online-p1 / online-p2local routes): no desync.
- [ ] PC-Switch session (cross-play): PC host from the test folder, Switch joins.
- [ ] Riders Plus charge jump on the Switch: hold A at cruise and right after a boost.
- [ ] Stock clocks with three swapchain images: fps, no new judder.
- [ ] `SR_PERFTEST=1` at stock clocks, handheld; 4-player Metal City with `SR_PROFILE=1`.
- [ ] Copy back `sonicriders.log`, `perftest.json`, `shader_keys.txt`.
