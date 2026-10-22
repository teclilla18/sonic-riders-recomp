# Sonic Riders native port (Switch homebrew, work in progress)

> [!IMPORTANT]
> **Your Switch needs to be overclocked for this to run well.** All testing so far was done at
> **CPU 1785 MHz / GPU 1152 MHz / memory 2133 MHz** (OLED model). At stock clocks the game runs
> poorly (slow motion, low frame rate). Overclock at your own risk; see [Requirements](#requirements).

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

## Install

1. Download the latest release zip from the [Releases](../../releases) page and extract it to a
   folder (a path without special characters works best).
2. Double-click **`SonicRidersSetup.bat`**. It asks for:
   - your disc image: **Sonic Riders (USA)**, game ID `GXEE8P`, revision 0, a clean 1:1 `.iso`
     (Redump "Sonic Riders (USA) (En,Ja,Fr,De,Es,It)", SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`).
     RVZ/GCZ/CISO images must be converted back to ISO with Dolphin first;
   - what to build: the Switch homebrew, a PC test build, or both;
   - your Switch's SD card (optional), where it copies the build and, once, your disc image.
3. On the Switch, start the Homebrew Menu **in title mode** (hold R while starting any installed game)
   and open **Sonic Riders**. Applet mode (the Album) has much less memory and is not supported.

What the setup does: checks your image's hash; downloads a pinned toolchain (Python, ninja,
decomp-toolkit, devkitPro's devkitA64/libnx/deko3d, and for the PC build llvm-mingw + SDL2; every file
is checked against a SHA-256 in `setup/toolchain.json`) into `%LOCALAPPDATA%\SonicRidersSetup\tools`;
extracts `main.dol` and `_Main.rel` from your image; analyses and recompiles them; builds; and copies
the result to the SD card. No administrator rights are needed. Everything is logged to `setup.log`.
Expect about 4 GB of disk space and 10–30 minutes the first time.

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

## Requirements

- **Switch:** a console running custom firmware (Atmosphère) with the Homebrew Menu; the disc image
  (1.4 GB) on its SD card.
- **An overclock is currently needed for good performance.** All testing so far was on an OLED
  overclocked to CPU 1785 MHz / GPU 1152 MHz / memory 2133 MHz (e.g. with sys-clk); at stock clocks
  the game runs much slower. Overclock at your own risk and within your console's limits.
- **PC (for building):** 64-bit Windows 10 (1803 or newer) or 11, an internet connection for the first
  run, about 4 GB free.
- **PC test build:** a GPU with OpenGL 3.3. This build is a development tool; the Switch is the target.

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

## Reporting bugs

Open an issue with the bug template. Attach `sonicriders.log` and `sonicriders.prev.log` from
`sdmc:/switch/sonicriders/` (they contain no game data), and a screenshot from the settings menu's
"Save screenshot" if it is visual. Never attach your disc image, saves, builds or keys.

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
