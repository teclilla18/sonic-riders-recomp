# Sonic Riders native port (Switch homebrew, work in progress)

> [!IMPORTANT]
> **Your Switch needs to be overclocked for this to run well.** All testing so far was done at
> **CPU 1785 MHz / GPU 1152 MHz / memory 2133 MHz** (OLED model). At stock clocks the game runs
> poorly (slow motion, low frame rate). Overclock at your own risk; see [What you need](#what-you-need).

An unofficial port of **Sonic Riders** (GameCube, USA) that runs natively on a modded Nintendo
Switch, built with **static recompilation**:

- the game's PowerPC code (`main.dol` + `_Main.rel`) is translated to C ahead of time, on your PC;
- the GameCube hardware (graphics, disc, audio DSP, memory card, controllers) is reimplemented natively;
- graphics are drawn with deko3d on the Switch (OpenGL 3.3 in the PC test build).

The approach follows [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD) and
[ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp), whose PowerPC→C translator
this project forks.

## Legal notice

- This is a fan project. It is **not affiliated with, endorsed or sponsored by SEGA or Nintendo**.
  "Sonic Riders" and "Nintendo Switch" are trademarks of their owners and are used only to describe
  what this software works with.
- **This repository and its releases contain no game code, assets, data or console keys.** You need
  your own disc image of Sonic Riders (USA), made from a disc you own. Everything derived from the
  game is generated locally on your PC by the setup tool.
- Builds made with this project contain recompiled game code. **Never share your built `.nro`, `.exe`
  or `.nsp`, or your disc image.** Issues and pull requests that contain game files are deleted.

## What you need

- **A Nintendo Switch running custom firmware** (Atmosphère) with the Homebrew Menu, and an SD card
  with about 2 GB free.
- **An overclock** (see the note at the top). Tested at CPU 1785 / GPU 1152 / memory 2133 MHz on an
  OLED, set with an overclocking homebrew such as sys-clk. Overclock at your own risk and within your
  console's limits.
- **Your own copy of Sonic Riders (USA)** dumped to a disc image: game ID `GXEE8P`, revision 0, a clean
  1:1 `.iso` (1,459,978,240 bytes, SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`, the Redump
  dump "Sonic Riders (USA) (En,Ja,Fr,De,Es,It)"). You can dump your disc with a Wii and CleanRip.
  RVZ/GCZ/CISO images must be converted back to a full ISO first (Dolphin: right-click the game,
  Convert File, format ISO). The European and Japanese versions are not supported.
- **A Windows 10 (version 1803 or newer) or Windows 11 PC**, 64-bit, with an internet connection and
  about 4 GB of free disk space. The PC does the conversion; nothing is downloaded from the game.

## Install (playing on your Switch)

1. **Download** `SonicRidersSetup-<version>.zip` from the [latest release](../../releases/latest).
   (Not the "Source code" links GitHub adds at the bottom.)
2. **Extract** the whole zip to a folder, for example `C:\SonicRiders`. Don't run it from inside the
   zip.
3. **Put your Switch's SD card in the PC** (or connect the Switch over USB with a tool that shows the
   SD card as a drive).
4. **Double-click `SonicRidersSetup.bat`.** If Windows shows "Windows protected your PC", click
   *More info* → *Run anyway* (the script is not signed). Then answer its questions:
   - pick your disc image in the file window; the setup checks that it is the right one;
   - choose **1** (Switch);
   - say **y** to copy to the SD card and pick the SD card's top folder (the one containing
     `atmosphere` and `switch`);
   - say **y** to copy your disc image to the SD card (needed once; skipped automatically later).
5. **Wait.** The first run downloads the build tools (about 300 MB, 1.3 GB unpacked, kept for next time) and converts the
   game: 10–30 minutes. It ends with "Done". If something fails, the message says why, and the whole
   log is in `setup.log`.
6. **Eject the SD card safely** and put it back in the Switch.
7. **Start it in full-memory mode:** hold **R** while starting any installed game until the Homebrew
   Menu opens, then choose **Sonic Riders**. Opening the Homebrew Menu from the Album does not give the
   game enough memory.

Hold **−** (Minus) in the game for the settings menu (widescreen, resolution, FPS counter, controllers).
Your saves and settings stay on the SD card in `switch/sonicriders/`.

**Updating to a new version:** download and extract the new release, run `SonicRidersSetup.bat` again
with the same disc image and SD card. Saves are kept.

### If something goes wrong

| Problem | What to do |
|---|---|
| "does not match the known-good dump" | Your image is not the clean USA rev 0 dump (trimmed, compressed or modified). Re-dump or convert to a full ISO. |
| "game ID is ..." | Only the USA release (GXEE8P) works. |
| A download fails | Check your internet connection or firewall and run the setup again; finished downloads are kept. |
| Black screen or crash at start on the Switch | Make sure you started from a game with R held (not the Album), and that the `.iso` is in `switch/sonicriders/`. |
| Slow motion / low frame rate | Check your overclock (see What you need). |
| Anything else | Open a [bug report](../../issues/new/choose) with `sonicriders.log` and `sonicriders.prev.log` from `switch/sonicriders/` on the SD card. |

### What the setup does

It checks your image's hash; downloads a pinned toolchain (Python, ninja, decomp-toolkit, devkitPro's
devkitA64/libnx/deko3d, and for the PC build llvm-mingw + SDL2; every file is checked against a SHA-256
in `setup/toolchain.json`) into `%LOCALAPPDATA%\SonicRidersSetup\tools`; extracts `main.dol` and
`_Main.rel` from your image; analyses and recompiles them into C; builds; and copies the result to the
SD card. No administrator rights are needed.

The SD card ends up with:

```
sdmc:/switch/sonicriders/
    sonicriders.nro             the port
    <your disc image>.iso       read at runtime (music, models, stages...)
    saves/card_a.raw            memory card (created on first save)
    settings.ini                settings menu choices
    sonicriders.log             log of the last run (sonicriders.prev.log: the one before)
```

**Optional home-screen icon:** `tools/nsp/` builds a small forwarder NSP that starts the `.nro` from
the home menu. It needs your own console keys and is for users who already know how to install NSPs;
see `tools/nsp/build_nsp.ps1`.

**PC test build:** choose 2 or 3 in the setup to also get a Windows build (OpenGL 3.3), started with
`Play on PC.bat`. It is a development tool; the Switch is the target.

## Building from source

The setup tool *is* the build: `SonicRidersSetup.bat` works the same from a `git clone`. For
non-interactive use:

```
powershell -ExecutionPolicy Bypass -File setup\setup.ps1 -Iso <image.iso> -Target switch|pc|both [-Sd E:\ | -NoSd] [-Yes]
```

The steps it runs, if you want to do them by hand (tools on `PATH`, devkitPro in `DEVKITPRO`):

```
python tools/extract_iso.py <image.iso> baserom                        # main.dol, _Main.rel (hash-checked)
dtk dol split config/GXEE8P/config.yml build/GXEE8P                    # decomp-toolkit 1.8.4 analysis
python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen        # PowerPC -> C
python configure.py --target switch --devkitpro <devkitPro dir>        # -> build/switch/sonicriders.nro
python configure.py --target host --toolchain mingw --llvm-mingw <dir> --sdl <SDL2 mingw dir>   # -> build/host-mingw/
ninja
```

The PC build plays the first `*.iso` in `baserom/` (or the image given on its command line).
After cloning, enable the game-data guard: `git config core.hooksPath tools/git-hooks`.
Design notes, findings and the current work log are in [NOTES.md](NOTES.md).

## Playing

### Controls

| GameCube | Switch (Pro Controller, Joy-Con pair, handheld) | PC keyboard | PC gamepad |
|---|---|---|---|
| Control stick | Left stick | Arrow keys | Left stick |
| C-stick | Right stick | I J K L | Right stick |
| A / B / X / Y | A / B / X / Y | X / Z / S / A | A / B / X / Y |
| START | + | Enter | Start |
| L / R | ZL / ZR | Q / W | Left / right trigger |
| Z | R | E | Right shoulder |
| D-pad | D-pad | T F G H | D-pad |

- **Settings menu:** hold **−** (Minus) for half a second on the Switch; F1 or the gamepad's Back
  button on PC. Items: Screen (4:3 / 16:9 widescreen / 16:9 stretched), Resolution (1x / 1.5x / 2x),
  FPS counter, Unlock Chaos Emerald gear, Controllers (pair or split Joy-Cons), Save screenshot.
- **Up to 4 players:** connect more controllers; each player joins at Player Entry with their own
  controller. A **single Joy-Con held sideways** works as one controller (SL/SR = L/R, stick click = Z,
  +/− = START; hold it for the settings menu). On PC, extra gamepads become players 2–4.
- HOME exits on the Switch.

### Debug options

An `env.txt` in `sdmc:/switch/sonicriders/` (one `NAME=VALUE` per line; environment variables on PC)
turns on diagnostics, for example `SR_PROFILE=1` (CPU profile in the log), `SR_SHOTS=1` (periodic
screenshots), `SR_WATCHDOG=<seconds>` (dump the guest's state when the game stops responding).
The full list is in NOTES.md.

## Status

Playable from boot to races, story mode and up to 4-player Free Race, with sound. The figures below
are with the overclock above; **at stock clocks performance is poor** (making that playable is an open
goal). Known issues:

- **3–4 player races run at 30 fps** on the Switch (1–2 players: mostly 60).
- Some stages (Egg Factory, Splash Canyon) drop below 60 fps in busy sections (GPU-bound). Specialized
  shaders built ahead of time help, but only for graphics states your own console has recorded, and
  a fresh build has none: after playing, copy `sdmc:/switch/sonicriders/shader_keys.txt` into
  `build\shaderkeys\` and run the setup again.
- The speed-blur screen effect is weaker than on GameCube; screen corners are darker; a few
  billboards are slightly see-through; some menu/HUD colours differ.
- The story-mode cutscene monitor looks garbled.
- A rare hang on a "Now Loading" screen (much less frequent since disc reads were reworked).
- The PC test build garbles a few race HUD elements.

See the CHANGELOG for what changed and NOTES.md for the details.

## Contributing

Help is welcome from players and developers alike: test on your Switch and report bugs, write code, or
point a coding agent such as **Claude Code** at an open issue (the repository includes a
[CLAUDE.md](CLAUDE.md) with the rules and test workflow agents need). Start with
[CONTRIBUTING.md](CONTRIBUTING.md) and the [help wanted](../../issues?q=is%3Aopen+label%3A%22help+wanted%22) issues.

## Reporting bugs

Open an issue with the [bug report form](../../issues/new/choose). The more of these it has, the
faster it gets fixed:

1. **What happened and where:** game mode, stage, character, number of players, and what you did just
   before.
2. **Your clocks** (overclocked or stock) and console model.
3. **The logs:** `sonicriders.log` (this run) and `sonicriders.prev.log` (the run before) from
   `switch/sonicriders/` on the SD card. If the game crashed or froze, copy them soon: each start
   moves the last log to `sonicriders.prev.log` and deletes the older one. They contain no game data.
4. **A screenshot** for anything visual (wrong colours, missing or see-through objects, garbled text).

### Taking a screenshot for a bug report

1. When the problem is on screen, **hold − (Minus)** for half a second to open the settings menu.
2. Choose **Save screenshot (A)** with the D-pad and press **A**. The menu closes and the next game
   frame is saved, without the menu in it. (The Switch's own screenshot button often doesn't work for
   homebrew.)
3. Take as many as you need; each one is a new file. Then put the SD card in your PC. The pictures are
   in `switch/sonicriders/`, named `shot_<number>.bmp` (the number is the frame, which also helps
   match the picture to the log).
4. GitHub doesn't accept `.bmp` files: open the picture in Paint and *Save as* PNG, or put the `.bmp`
   files in a zip. Then drag them into the bug report.

It helps to say what it *should* look like: a photo or video of the same spot on a GameCube, or a
Dolphin screenshot.

Never attach your disc image, saves, builds (`.nro`) or keys.

## License

The project is licensed under the [Mozilla Public License 2.0](LICENSE), the same as the
ZeldaWWHDRecomp translator it forks (`third_party/LICENSE-MPL-2.0`). Third-party components keep their
own licenses (see Credits).

## Credits

- [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD) and
  [ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp): the approach, and the
  PowerPC→C translator this project forks (MPL-2.0).
- [Dolphin](https://dolphin-emu.org/): hardware documentation and behaviour reference (no Dolphin
  code is included).
- [decomp-toolkit](https://github.com/encounter/decomp-toolkit): analysis of the game's executables.
- [devkitPro](https://devkitpro.org/), [libnx](https://github.com/switchbrew/libnx) and
  [deko3d](https://github.com/devkitPro/deko3d): the Switch toolchain and graphics API.
- [nx-hbloader](https://github.com/switchbrew/nx-hbloader) (ISC) and
  [hacBrewPack](https://github.com/The-4n/hacBrewPack): the optional forwarder NSP.
- [SDL2](https://www.libsdl.org/) and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): the PC test build.
- [stb_easy_font](https://github.com/nothings/stb) (public domain / MIT): the settings menu's text.
- gamemasterplc's 16:9 Gecko code: the idea behind the widescreen option.

Sonic Riders is © SEGA. This project is not affiliated with SEGA.
