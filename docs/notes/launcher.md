# Windows launcher (2026-11-07, implemented, not released)

`launcher/launcher.cpp` → `SonicRidersLauncher.exe`, modelled on Tag Force Recomp's launcher (notes/plans-2026-11.md).
It holds no game code or data, so a ready-made copy can go in the PC release zip (packaging not done yet).

- Build: `python tools/build_launcher.py [--root-copy]` (llvm-mingw clang++ and Dear ImGui from the setup's tools
  folder; Win32 + Direct3D 11, static, ~1.3 MB, no DLLs). It must live in the port's root folder (next to setup\,
  build\, baserom\, saves\, mods\): `--root-copy` puts it there (the root copy is ignored by git). Started from a subfolder (build\launcher\) it
  uses the nearest parent folder with setup\setup.ps1 as the root.
- Pages: Play (ready check: game built, disc image in baserom\, saves\card_a.raw, graphics card; quick settings),
  Graphics (screen mode, HUD stretch, FOV 80-130, window mode, frame rate, frame counter), Game (Chaos Emerald,
  saves folder), Mods (on/off + order, writes mods\mods.ini), Controls (button map, below), About
  (version from VERSION, links, Rebuild).
- Settings: reads and writes `saves\settings.ini` (the in-game menu's file; unknown keys kept), saved 0.5 s after a
  change and before PLAY; read again when the game closes.
- First run: "Select disc..." + INSTALL AND PLAY runs `setup\setup.ps1 -Target pc -NoSd -Yes -DesktopShortcut no
  -Iso <file>` hidden, output in a log box, progress from its "== N/7" step lines. PLAY starts
  `build\host-mingw\sonicriders.exe` (Stop ends it). Install path not run end to end yet.
- Banner: `launcher\banner.jpg`, public fan art chosen by the user (1800x520 crop of a 1920x1080 picture; not from
  the game). Committed with the user's OK (2026-11-07); the guard allows exactly this file by SHA-1
  (OWN_ART in tools/check_no_game_data.py), so a changed banner needs a new hash there. Without it the banner is a blue gradient.
- Testing: `SR_LAUNCHER_SHOT=<file.png>` (+ `SR_LAUNCHER_PAGE=0-5`) draws off-screen, saves the picture after
  20 frames and quits (look at the launcher this way, not with desktop screenshots).
  `SR_LAUNCHER_TEST="<s>[;key=value,...]"` (off-screen): sets settings (`fov=111`) and buttons (`key.A=Space`,
  `pad.Z=leftshoulder`) as the pages would, presses PLAY, Stop after <s> seconds, writes build\launcher\test.txt
  (start, exit code, settings read back). With `SR_MINIMIZED=1` (inherited by the game) and an `SR_INPUT` script
  using `M.` menu buttons it tests the F1 menu round trip (2026-11-07: fov 111 reached the game, the scripted menu
  turned the Chaos Emerald option on, the launcher read it back after Stop [confirmed]).
- Button map: `saves\controls.ini` (`key.<IN>=<SDL key name>`, `pad.<IN>=<SDL controller button>` / lefttrigger /
  righttrigger / none; IN = A B X Y Z L R START DPAD_* STICK_* C_*), read once at game start by
  input_sdl.c controls_load (log line `[input] saves/controls.ini: N entries`; unknown names logged and skipped).
  Controller sticks are not remapped; one map for all controllers; keyboard = player 1. The Controls page: click a
  box, press a key (F1/F11 not allowed) or an XInput button (Xbox-style pads via xinput1_4.dll), or pick from the
  list; clashes in red. Tested: the launcher's file read with all 32 entries [confirmed]; real key presses in a
  race not tested by us.
- Icon (2026-11-09, user's choice): `launcher/icon_256.png` + `icon_32.png` (a tighter face crop), Super Sonic art the user made
  with ChatGPT, pinned in the guard's OWN_ART; build_launcher.py packs them into an .ico. Without that file build_launcher.py falls back to tools/launcher_icon.py's gold ring.
  build_launcher.py compiles it in as resource APPICON.
- Next:
  later Switch/Android pages. Done 2026-11-07: the exe in the PC zip (make_release.py builds it from the committed
  source), cold Sandbox install through the launcher PASS (`sandbox_test.py --launcher`, SR_LAUNCHER_INSTALL).
