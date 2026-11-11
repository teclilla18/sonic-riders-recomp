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
  build_launcher.py compiles it in as resource APPICON. **Every launcher uses this art** (user, 2026-11-09): the PC
  launcher, the Switch / Android installer windows (same exe) and the Mac launcher (build_mac_launcher.py makes
  AppIcon.icns from the same two PNGs, sizes up to 256 px, into build/; gold-ring fallback likewise). Not the
  official-art SonicRidersFanIcon.png (local builds only).
- Next:
  later Switch/Android pages. Done 2026-11-07: the exe in the PC zip (make_release.py builds it from the committed
  source), cold Sandbox install through the launcher PASS (`sandbox_test.py --launcher`, SR_LAUNCHER_INSTALL).

## Installer mode (Switch, Android; 2026-11-09)

The same exe, shipped in the Switch / Android zips as `Install Sonic Riders (<Plat>).exe` (make_release.py), turns
into an installer when its file name contains "switch" or "android" (`SR_INSTALLER=switch|android` for tests).
Pages: Install + About. It asks what setup.ps1 asks and runs it unattended: `-Yes -Iso <disc> -Target switch
[-Sd <card> -CopyIso yes|no | -NoSd] -HomeIcon yes -Keys <prod.keys> | -HomeIcon no [-Icon <picture>]`, or
`-Target android -AcceptAndroidSdkLicense [-Install no] [-Icon ...]`. SD cards: drives with `atmosphere` (or a
removable drive with `switch`); prod.keys pre-filled from `%USERPROFILE%\.switch\prod.keys`. The console setup stays
as `Install Sonic Riders (<Plat>) - command line.bat` (sandbox_test.py falls back to it).
Test: `SR_INSTALLER=switch SR_LAUNCHER_INSTALL=<iso> [SR_INSTALLER_SD=<card>]` builds with the defaults, writes
launcher_test.txt and exits 0 on success; `SR_INSTALLER=... SR_LAUNCHER_SHOT=<png>` for a picture.

## Updates (2026-11-09)

`update_core.h` fetches the newest release (tag, notes as plain text, the address of this flavour's zip:
`SonicRiders-<PC|Switch|Android>-Setup-<tag>.zip`). The sidebar's gold "See what's new" opens the Update page
(P_UPDATE, listed only when there is one): notes, DOWNLOAD AND INSTALL (WinHTTP to build\update\update.zip with a
progress bar), then `build\update\apply.ps1` (written by the launcher): Expand-Archive, the running exe renamed to
`.old` (deleted at the next start), robocopy `/E /IS /IT /IM` of the zip's top folder over the root (without these
robocopy skips same-size, same-time files; /IM because newer Windows files them as "modified"; release zips carry the commit
time for the same reason, so older launchers copy everything too: bug fixed in v0.7.3), then RESTART. A PC update leaves
`build\launcher\rebuild_after_update`: the Play page offers "Rebuild now". The game's F1 > About shows the notes.
Tests: `SR_LAUNCHER_FAKE_VERSION=v0.7.0` (pretend to be older), `SR_LAUNCHER_UPDATE_TEST=1` (press the button,
write build\update_test.txt, exit 0 when updated). 2026-11-09: PASS from an unpacked v0.7.1 PC zip and Switch zip
(as v0.7.0): real download from GitHub, exe swapped, saves kept, marker written.

