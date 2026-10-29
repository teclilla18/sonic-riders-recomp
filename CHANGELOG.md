# Changelog

## v0.3.3 (2026-10-29)

- **Switch: characters are no longer tinted blue/teal (#11).** Lit 3D models lost most of their red
  on the Switch only: white gloves and shoes came out cyan, skin olive, in menus and in every race.
  The Switch's shader compiler mishandled the lighting code; it is restructured so the lighting
  now matches the GameCube (checked on hardware against the original game in Dolphin).

## v0.3.2 (2026-10-26)

- **Switch: fixed a large slowdown after the first few minutes.** After some races the game leaves a
  rarely used color setting switched on, and from then on the port stopped using its fast
  precompiled shaders for everything: menus and races took several times longer on the GPU (about
  20 fps at stock clocks). The fast shaders now keep working for the whole session.
- Faster translated game code on Switch: cheaper function calls and memory accesses (7% smaller code).

## v0.3.1

- **Switch: playable without an overclock.** At stock clocks, races now hold a steady 30 fps (tested
  on hardware in handheld mode, including the busiest views). Two changes made this possible:
  - The vertex shader reads the game's matrices and lights from fast GPU memory: about 27% less GPU
    time per race frame on hardware (e.g. 15 ms down to 11 ms).
  - The game's vertices are converted on a second CPU core that was otherwise waiting for the GPU:
    the thread that turns the game's drawing commands into GPU work went from 18-19 ms to 8-13 ms
    per frame in busy races.
- **The "60 fps" setting no longer slows the game down.** It now draws 60 fps while the console
  keeps up and switches to a steady 30 fps at full speed in scenes too busy for 60, going back to 60
  when the scene allows it. On a Switch at stock clocks, light parts of a race run at 60 and the
  busiest ones at 30.
- The Switch menu has no Quit Game button (close the game from HOME).
- The FPS counter alone no longer blends a full-screen layer each frame (less GPU work), and the
  menu is drawn on a helper thread (no 30 ms hitches when moving the cursor on the Switch).
- 30 fps chosen in the middle of a race now also runs effects at the right speed straight away.
- The log records every settings change and the GPU's time per frame, for hardware reports.
- **New settings menu** in the style of SwitchWakerHD's, in Sonic colours: tabs (Display,
  Performance, Controls, Gameplay, Mods, About) switched with L / R, a gold cursor, a short
  explanation of the selected option, the Switch's own font, and (on PC) a Quit Game button that asks first.
- **No more slow motion when the game is busy.** New setting **Frame rate**: *Auto (keep speed)*,
  the default, draws 60 fps while it can and, when a frame takes too long, leaves out drawing a
  frame instead of slowing the whole game down, so races always run at their real speed (the game
  keeps 60 steps a second; you see fewer frames, e.g. 35-55 fps, instead of 30 fps in slow motion).
  *30 fps (full speed)* uses Sonic Riders' own unused 30 fps mode: a steady 30 fps at full speed.
  *60 fps* (originally the game's own behaviour, slow motion when late; changed in v0.3.1, see above).
- **Faster game code** on both platforms (most on the Switch): the translated game code is about
  30% smaller on the Switch and does less work per instruction (ideas from SwitchWakerHD); the intro
  movie's video decoding is much cheaper. Pictures are unchanged.
- Switch: fixed crackling sound in busy races at stock clocks (the sound mixer now gets the CPU before
  the renderer; not yet confirmed on hardware). Slightly less work per frame for the renderer.
- Switch: in handheld mode the GPU uses Nintendo's own 384 MHz setting for games instead of the
  system's 307 MHz (not an overclock; docked is unchanged).
- **PC: file mods.** A `mods` folder next to the game replaces or adds the game's files (character
  models, textures, music, stages) without touching your disc image; `mods/mods.ini` sets the order
  and turns mods off; the settings menu has a **Mods** page to turn them on and off. See
  [docs/MODDING.md](docs/MODDING.md).
- **PC: stat mods.** A mod's `mod.ini` can change Extreme Gear stats by name (`[gear.HighBooster]`
  `level1_boost_speed_kmh = 260`), write memory, and run simple Gecko codes. New tracks are planned.
- The README is shorter; installation, playing, bug reports, building and modding have their own
  pages in `docs/`.
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
