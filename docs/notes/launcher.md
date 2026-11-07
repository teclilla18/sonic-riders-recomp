# Windows launcher (2026-11-07, implemented, not released)

`launcher/launcher.cpp` → `SonicRidersLauncher.exe`, modelled on Tag Force Recomp's launcher (notes/plans-2026-11.md).
It holds no game code or data, so a ready-made copy can go in the PC release zip (packaging not done yet).

- Build: `python tools/build_launcher.py [--root-copy]` (llvm-mingw clang++ and Dear ImGui from the setup's tools
  folder; Win32 + Direct3D 11, static, ~1.3 MB, no DLLs). It must live in the port's root folder (next to setup\,
  build\, baserom\, saves\, mods\): `--root-copy` puts it there (the root copy is ignored by git).
- Pages: Play (ready check: game built, disc image in baserom\, saves\card_a.raw, graphics card; quick settings),
  Graphics (screen mode, HUD stretch, FOV 80-130, window mode, frame rate, frame counter), Game (Chaos Emerald,
  saves folder), Mods (on/off + order, writes mods\mods.ini), Controls (layout text; remapping not yet), About
  (version from VERSION, links, Rebuild).
- Settings: reads and writes `saves\settings.ini` (the in-game menu's file; unknown keys kept), saved 0.5 s after a
  change and before PLAY; read again when the game closes.
- First run: "Select disc..." + INSTALL AND PLAY runs `setup\setup.ps1 -Target pc -NoSd -Yes -DesktopShortcut no
  -Iso <file>` hidden, output in a log box, progress from its "== N/7" step lines. PLAY starts
  `build\host-mingw\sonicriders.exe` (Stop ends it). Install path not run end to end yet.
- Banner: `launcher\banner.jpg`, public fan art chosen by the user (1800x520 crop of a 1920x1080 picture; not from
  the game). The guard refuses binary files, so it is not committed yet (needs the user's OK for an exact-SHA-1
  allow entry like android/touch_icons). Without it the banner is a blue gradient.
- Testing: `SR_LAUNCHER_SHOT=<file.png>` (+ `SR_LAUNCHER_PAGE=0-5`) draws off-screen, saves the picture after
  20 frames and quits (look at the launcher this way, not with desktop screenshots).
- Next: commit the banner (user), app icon, button remapping on Controls, the exe in make_release.py's PC zip (+
  README-FIRST), an end-to-end install test in Windows Sandbox, later Switch/Android pages.
