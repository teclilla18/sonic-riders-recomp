# State (2026-11-21, night): native Online Beta 1 candidate

This branch is the separate native Switch 2 candidate, based on the public Alpha 3 tag 67710d8.
It is not merged to main and has not been released. Main-based beta: `codex/online-beta`.

## Implemented

- Shared Online Beta 1 LAN lobby, chosen player names and NET_COMPAT 1/game protocol 5.
- Black, silent startup screen until Mode Select; two players on the same home network.
- Dedicated online startup (SR_ONLINE_DEFAULT); controller keyboard for names.
- Own app folder: `/switch/sonicriders-switch2-online/sonicriders-switch2-online.nro` (+ build-info.json). The
  homebrew menu shows one app per folder; data (disc, env.txt, settings, saves, caches) stays in
  `/switch/sonicriders-switch2/` (fixed BASE_DIR). Setup never touches the normal app (7cc264a).
- Lobby boot on a helper thread when the lobby is requested (main.c `boot_rest`): the render loop runs on the main
  thread on Switch 2/iOS, so the lobby used to wait forever (black screen) (8fc5e3e).
- Loading screen black via a Q_FILL quad instead of vkCmdClearAttachments on the swapchain (8fc5e3e; did not fix the crash below).
- Package audit baseline: released Alpha 3 (67710d8, build bc9dd55883f6; user's choice) + listed Online Beta files (b6654d2).

## Tested and limits

- User's Switch 2 (handheld): lobby shows, PC joins by discovery, 708 checksums equal [confirmed].
- **Open blocker: GPU device loss at game frame 712, every time (3 of 3 runs).** Frame 662 loads a 1 MB title
  texture; ~50 frames later `MESA: completion join failed status 7`, `[vk] device lost`, renderer rebuild fails
  (`vkCreateDevice -4`), app closes. Not the cause: 120 fps projection mode (SR_FPS=0) and disc read-ahead
  (SR_DISC_READAHEAD=0), both ruled out in run 3; not the swapchain clear. Logs: build/ftp-online/crash*-20261121/.
- Known good: 2026-11-20 the Switch 2 hosted a 22,618-frame LAN race started with a direct `SR_NET=host:7777`
  (no lobby, no loading screen, normal boot order).
- Cold Sandbox of the Switch 2 package: PASS (7cc264a layout). Package not rebuilt since 8fc5e3e.
- Music loader is absent. Texture-pack first-use stutter and #35 remain known issues.

## Next

1. Isolate frame 712: direct-SR_NET host (lobby skipped). Careful: netplay.c waits call online_present_blank on the
   main thread before the Switch 2 render loop exists; make that path safe first. Then: lobby without the loading
   screen / auto-walk. Candidates left: boot order (render loop before load_game/vi_init), loading screen, the walk.
2. User wants an immediate "Starting..." picture (5 s blank before the lobby) and the Online logo as the package icon.
3. Rebuild package, audit, cold Sandbox (permission given 2026-11-21), then ask before any release or post.
Notes: docs/notes/online-beta.md.
