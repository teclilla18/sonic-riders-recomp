# Changelog

## Unreleased

- **The picture is no longer dark and blue-tinted (#11), and the speed blur shows (#3).** The game's
  speed-blur effect draws the previous frame over the screen edges; the port fed it the wrong
  picture (old memory), which darkened and tinted the whole screen, characters included. Races now
  look like they do on a GameCube.
- **Distance fog** is drawn (it was missing in every race).
- **Characters have their shine back (#11).** Characters looked flat and dark blue because their
  specular highlights were never drawn (a light at position 0,0,0 made the lighting maths divide by
  zero). Sonic's quills and the other characters now get the light-blue highlights the GameCube shows.

## v0.2.4 (2026-10-23)

- PC: the settings menu no longer shows the "Controllers" row; it only did something on the
  Switch (PC controllers are picked up automatically).
- PC: controller sticks are much less touchy in menus. A light touch on an Xbox-style stick used
  to send the cursor racing through menus; the stick now has a round dead zone (25%) and a smooth
  ramp after it, and a full push still steers fully.
- Switch: **sleep mode works.** Putting the console to sleep during the game and waking it used to
  leave the game frozen; it now carries on where it was (confirmed on hardware).
- **Faster multiplayer on the Switch:** the game's audio is now mixed on its own thread, as the
  GameCube's audio chip does (it took about 2 ms of every frame from the game). Busy parts of 3-player
  Metal City went from about 37 to around 50 fps (mostly 60 elsewhere). The graphics command path
  also no longer makes two CPU cores fight over the same memory (a small gain). 4 players not
  measured yet.

## v0.2.3 (2026-10-23)

- **PC: game controllers work now.** Controllers were detected but none of their buttons or sticks
  reached the game (only the keyboard worked). Analog triggers work too.
- The settings menu opens when F1 (or the controller's Back/View button) is **held** for half a
  second; the docs said "press".
- The PC log records the first controller button presses, to help with bug reports.
- **PC: the program has an icon** (Sonic, the game's own save icon), made from your disc during
  setup, for the program, its shortcuts, the window and the taskbar.

## v0.2.2 (2026-10-23)

- **Setup fix:** on some PCs the setup stopped with `Error opening archive: Can't initialize filter;
  unable to run program "xz -d -qq"`. Windows' built-in `tar` can't unpack some of the downloaded
  tools on many Windows versions; the setup now unpacks them itself and needs no other programs.
- README: the minimum Windows version and requirements are now at the top.

## v0.2.1 (2026-10-23)

- **PC keyboard controls changed:** WASD moves, J / K / L / I = A / B / X / Y, U / O / P = L / R / Z,
  Enter = START, arrow keys = C-stick, T / F / G / H = D-pad. The settings menu (F1) also takes
  WASD, J (accept) and K (back).
- The v0.2.0 palette fix is now confirmed on Switch hardware: trick rank letters, track select
  previews, the results screen, the pause and end-of-race menus all have the right colours, the white
  boxes around results icons are gone, and the 4-player UI no longer flickers.

## v0.2.0 (2026-10-22)

### New: play on Windows PC
- A separate **PC download** (`SonicRiders-PC-Setup`): builds a native Windows version from your own
  disc image, no emulator. The Switch download is now `SonicRiders-Switch-Setup`.
- The PC version is a normal windowed program (no console window; its log is `sonicriders.log` in the
  install folder), with vsync, borderless fullscreen (F11 / Alt+Enter, or the settings menu), a
  "Sonic Riders" shortcut in the install folder and optionally on the desktop.

### Fixes and improvements

- Setup: the game code is recompiled once per run, not twice; re-running the setup with nothing
  changed finishes in seconds.
- Fixed the rainbow-coloured trick rank letters (and other UI textures with the wrong palette):
  palette addresses are now masked to physical memory like the hardware does.
- Palettes are captured when the game loads them, not later on the graphics thread.
- Faster vertex decoding on the GP thread (about 40% less decode time, identical results): a step
  towards 60 fps in 3-4 player races.
- Quieter recompiler output: one summary line; details (unhandled data words, FPSCR notes) go to
  `build/gen/report.txt`.

## v0.1.0 (2026-10-22)

First public version. Everything below is built from the user's own disc image; no game data is
distributed.

### Recompilation and boot
- PowerPC → C static recompiler (forked from ZeldaWWHDRecomp's translator) for `main.dol` and
  `_Main.rel`, driven by decomp-toolkit's analysis; code patches in `config/GXEE8P/patches.txt`.
- The game's own OS, scheduler, alarms and SDK libraries run recompiled; only the thread switch,
  REL linking and a few hardware-only routines are native.
- Register-level models of VI, DI (disc), CP/PE/PI (graphics FIFO), DSP/ARAM/AI, EXI (memory card,
  IPL SRAM/RTC) and SI (controllers).
- Disc reads arrive progressively during the transfer, as on hardware (fixes the 4-player stage-load
  crash; the game's streaming decompressor relies on it).
- Story-mode crash fixed (interrupt handlers no longer run below the thread's stack).

### Graphics
- deko3d renderer (Switch) and OpenGL 3.3 renderer (PC) sharing one GX ubershader; TEV, indirect
  texturing, EFB copies, destination alpha with dual-source blending (fixes see-through surfaces).
- Ahead-of-time specialized fragment shaders (deko3d) from recorded graphics-state keys.
- GX processing on its own thread; indexed draws; pipelined frames (CPU/GPU overlap).
- Texture cache (two-way set, palette-aware keys).
- 16:9 widescreen (native form of the known Gecko code), 4:3, stretched; resolution 1x/1.5x/2x.

### Input and multiplayer
- Up to 4 players: Pro Controller, Joy-Con pair, handheld, single sideways Joy-Con; the system
  controller applet from the settings menu. PC: keyboard and up to 4 gamepads.

### Audio
- Native AX DSP microcode (ADPCM/PCM from ARAM, resampling, envelopes, filters, aux buses) with
  audout output on the Switch and SDL on PC.

### Saves and settings
- Memory card emulated as `saves/card_a.raw`, written in the background.
- Settings menu (hold −): screen mode, resolution, FPS counter, Chaos Emerald gear unlock,
  controllers, screenshots; saved to `settings.ini`.

### Tools
- Windows setup tool (`SonicRidersSetup.bat`): checks the disc image, downloads a SHA-256-pinned
  toolchain, recompiles and builds the Switch and/or PC build, installs to the SD card.
- Optional home-screen forwarder NSP (`tools/nsp/`).
- Diagnostics: CPU profiler, watchdog dumps, hitch and performance logging, scripted input,
  screenshots (see NOTES.md).
- Guard against game data and personal details in git: pre-commit hook and GitHub Actions
  (`tools/check_no_game_data.py`), also used by `tools/make_release.py`.
