# Sonic Riders native port (Nintendo Switch and Windows PC, work in progress)

> [!IMPORTANT]
> **On the Switch, an overclock is needed for this to run well.** All Switch testing so far was done at
> **CPU 1785 MHz / GPU 1152 MHz / memory 2133 MHz** (OLED model). At stock clocks the game runs
> poorly (slow motion, low frame rate). Overclock at your own risk; see [What you need](#what-you-need).
> The PC version ran at a steady 60 fps on the test PC (RTX 3060), 4 players included.

**Requirements at a glance** (details in [What you need](#what-you-need)):

- **Windows 10 (version 1803 or newer) or Windows 11, 64-bit**, for both versions: the setup builds
  the game on your PC. Nothing else to install: the setup downloads its own tools (no Python, Visual
  Studio, devkitPro or 7-Zip needed), about 4 GB of free disk space, internet the first time.
- **Your own Sonic Riders (USA) disc image**, `GXEE8P` revision 0, a full 1.4 GB `.iso`.
- **Switch:** custom firmware (Atmosphère) with the Homebrew Menu, and an overclock (see above).
- **PC:** a graphics card with OpenGL 3.3.

An unofficial port of **Sonic Riders** (GameCube, USA) that runs **natively**, with no emulator, on a
modded **Nintendo Switch** or on a **Windows PC**. It is built with **static recompilation**:

- the game's PowerPC code (`main.dol` + `_Main.rel`) is translated to C ahead of time, on your PC;
- the GameCube hardware (graphics, disc, audio DSP, memory card, controllers) is reimplemented natively;
- graphics are drawn with deko3d on the Switch and OpenGL 3.3 on the PC.

The approach follows [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD) and
[ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp), whose PowerPC→C translator
this project forks.

## Choose your version

Each [release](../../releases/latest) has one download per platform. Both build the game on your
Windows PC from **your own** disc image; neither contains any game files.

| I want to play on... | Download | Then follow |
|---|---|---|
| a modded **Nintendo Switch** | `SonicRiders-Switch-Setup-<version>.zip` | [Install on Switch](#install-on-switch) |
| my **Windows PC** | `SonicRiders-PC-Setup-<version>.zip` | [Install on PC](#install-on-pc) |

(Ignore the "Source code" links GitHub adds at the bottom of every release.)

## Updating to a new version

Fixes come out as new releases. To update an existing install:

1. Download the new zip for your platform from the [latest release](../../releases/latest) and extract it
   to a **new** folder.
2. Run its installer (`Install Sonic Riders (Switch).bat` or `Install Sonic Riders (PC).bat`) with the
   same disc image (and, for the Switch, the same SD card).
3. It reuses the build tools it downloaded last time, so it's quicker than the first install (a few
   minutes).

Your saves and settings are kept: on the Switch they're on the SD card, which the update doesn't touch.
On PC they're in the old install folder: copy its `saves` folder into the new one, then delete the old
folder.

To get a notification when a new version comes out, click **Watch** at the top of the GitHub page,
then **Custom** → **Releases**. What changed in each version is in the [CHANGELOG](CHANGELOG.md).

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

**For both versions:**

- **Your own copy of Sonic Riders (USA)** dumped to a disc image: game ID `GXEE8P`, revision 0, a clean
  1:1 `.iso` (1,459,978,240 bytes, SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`, the Redump
  dump "Sonic Riders (USA) (En,Ja,Fr,De,Es,It)"). You can dump your disc with a Wii and CleanRip.
  RVZ/GCZ/CISO images must be converted back to a full ISO first (Dolphin: right-click the game,
  Convert File, format ISO). The European and Japanese versions are not supported.
- **A Windows 10 (version 1803 or newer) or Windows 11 PC**, 64-bit, with an internet connection the
  first time and about 4 GB of free disk space. The PC does the conversion; nothing is downloaded from
  the game.

**For the Switch:**

- **A Nintendo Switch running custom firmware** (Atmosphère) with the Homebrew Menu, and an SD card
  with about 2 GB free.
- **An overclock** (see the note at the top). Tested at CPU 1785 / GPU 1152 / memory 2133 MHz on an
  OLED, set with an overclocking homebrew such as sys-clk. Overclock at your own risk and within your
  console's limits.

**For the PC:**

- A graphics card with **OpenGL 3.3** (any from the last ten years). Tested on an NVIDIA RTX 3060.
- A game controller is recommended (Xbox-style, through SDL; up to 4 for multiplayer). Keyboard works too.

## Install on Switch

1. **Download** `SonicRiders-Switch-Setup-<version>.zip` from the [latest release](../../releases/latest).
2. **Extract** the whole zip to a folder, for example `C:\SonicRiders`. Don't run it from inside the zip.
3. **Put your Switch's SD card in the PC** (or connect the Switch over USB with a tool that shows the
   SD card as a drive).
4. **Double-click `Install Sonic Riders (Switch).bat`.** If Windows shows "Windows protected your PC",
   click *More info* → *Run anyway* (the script is not signed). Then answer its questions:
   - pick your disc image in the file window; the setup checks that it is the right one;
   - say **y** to copy to the SD card and pick the SD card's top folder (the one containing
     `atmosphere` and `switch`);
   - say **y** to copy your disc image to the SD card (needed once; skipped automatically later).
5. **Wait.** The first run downloads the build tools (about 300 MB, 1.3 GB unpacked, kept for next
   time) and converts the game: 10–30 minutes. It ends with "Done". If something fails, the message
   says why, and the whole log is in `setup.log`.
6. **Eject the SD card safely** and put it back in the Switch.
7. **Start it in full-memory mode:** hold **R** while starting any installed game until the Homebrew
   Menu opens, then choose **Sonic Riders**. Opening the Homebrew Menu from the Album does not give the
   game enough memory.

Hold **−** (Minus) in the game for the settings menu (widescreen, resolution, FPS counter, controllers).
Your saves and settings stay on the SD card in `switch/sonicriders/`.

## Install on PC

1. **Download** `SonicRiders-PC-Setup-<version>.zip` from the [latest release](../../releases/latest).
2. **Extract** the whole zip to a folder **you'll keep**, for example `C:\Games\SonicRiders`. The game,
   your saves and your settings live in that folder.
3. **Double-click `Install Sonic Riders (PC).bat`.** If Windows shows "Windows protected your PC",
   click *More info* → *Run anyway*. Pick your disc image, and say **y** if you want a desktop shortcut.
4. **Wait.** The first run downloads the build tools (a few hundred MB, kept for next time) and builds
   the game: 10–30 minutes. It ends with "Done".
5. **Play:** start **Sonic Riders** with the shortcut in the folder (or on your desktop).

| On PC | Keyboard | Controller |
|---|---|---|
| Move | W A S D | Left stick |
| A / B / X / Y | J / K / L / I | A / B / X / Y |
| START | Enter | Start / Menu |
| L / R | U / O | Left / right trigger |
| Z | P | Right shoulder |
| C-stick | Arrow keys | Right stick |
| D-pad | T F G H | D-pad |
| Settings menu | hold F1 | hold Back / View |
| Fullscreen | F11 or Alt+Enter | (settings menu) |

More controllers join as players 2–4. The game's log is `sonicriders.log` in the install folder (attach
it to bug reports).

### If something goes wrong

| Problem | What to do |
|---|---|
| "does not match the known-good dump" | Your image is not the clean USA rev 0 dump (trimmed, compressed or modified). Re-dump or convert to a full ISO. |
| "game ID is ..." | Only the USA release (GXEE8P) works. |
| A download fails | Check your internet connection or firewall and run the setup again; finished downloads are kept. |
| `Error opening archive: Can't initialize filter; unable to run program "xz -d -qq"` | Fixed in v0.2.2: download the latest release and run its installer. |
| Switch: black screen or crash at start | Make sure you started from a game with R held (not the Album), and that the `.iso` is in `switch/sonicriders/`. |
| Switch: slow motion / low frame rate | Check your overclock (see What you need). |
| PC: the game doesn't start | Check `sonicriders.log` in the install folder; update your graphics driver (OpenGL 3.3 is needed). |
| PC: no sound | Check Windows' default playback device; the game uses it. |
| Anything else | Open a [bug report](../../issues/new/choose) with the log (Switch: `sonicriders.log` and `sonicriders.prev.log` from `switch/sonicriders/` on the SD card; PC: the same files in the install folder). |

### What the setup does

It checks your image's hash; downloads a pinned toolchain (Python, ninja, decomp-toolkit, and either
devkitPro's devkitA64/libnx/deko3d for the Switch or llvm-mingw + SDL2 for the PC; every file is
checked against a SHA-256 in `setup/toolchain.json`) into `%LOCALAPPDATA%\SonicRidersSetup\tools`;
extracts `main.dol` and `_Main.rel` from your image; analyses and recompiles them into C; builds; and
(Switch) copies the result to the SD card. No administrator rights are needed.

The SD card ends up with:

```
sdmc:/switch/sonicriders/
    sonicriders.nro             the port
    <your disc image>.iso       read at runtime (music, models, stages...)
    saves/card_a.raw            memory card (created on first save)
    settings.ini                settings menu choices
    sonicriders.log             log of the last run (sonicriders.prev.log: the one before)
```

The PC install folder ends up with `Sonic Riders.lnk` (the shortcut), `build\host-mingw\sonicriders.exe`
(the game), `baserom\` (a link to your disc image), `saves\` (memory card and settings) and
`sonicriders.log`.

**Optional home-screen icon (Switch):** `tools/nsp/` builds a small forwarder NSP that starts the `.nro`
from the home menu. It needs your own console keys and is for users who already know how to install
NSPs; see `tools/nsp/build_nsp.ps1`.

## Building from source

The setup tool *is* the build: `SonicRidersSetup.bat` (it asks Switch or PC) works the same from a
`git clone`. For
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
| Control stick | Left stick | W A S D | Left stick |
| C-stick | Right stick | Arrow keys | Right stick |
| A / B / X / Y | A / B / X / Y | J / K / L / I | A / B / X / Y |
| START | + | Enter | Start |
| L / R | ZL / ZR | U / O | Left / right trigger |
| Z | R | P | Right shoulder |
| D-pad | D-pad | T F G H | D-pad |

- **Settings menu:** hold **−** (Minus) for half a second on the Switch; hold F1 or the
  gamepad's Back button on PC the same way. Items: Screen (4:3 / 16:9 widescreen / 16:9 stretched), Resolution (1x / 1.5x / 2x),
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

Playable from boot to races, story mode and up to 4-player Free Race, with sound, on the Switch and
on PC. Switch figures are with the overclock above; **at stock clocks performance is poor** (making
that playable is an open goal). The PC version ran at 60 fps on the test PC, 4 players included. Known issues:

- **3–4 player races run at 30 fps** on the Switch (1–2 players: mostly 60).
- Some stages (Egg Factory, Splash Canyon) drop below 60 fps in busy sections (GPU-bound). Specialized
  shaders built ahead of time help, but only for graphics states your own console has recorded, and
  a fresh build has none: after playing, copy `sdmc:/switch/sonicriders/shader_keys.txt` into
  `build\shaderkeys\` and run the setup again.
- Characters look flatter and darker than on GameCube (lighting); the speed-blur screen effect is
  weaker; screen corners are darker; a few billboards are slightly see-through.
- The story-mode cutscene monitor looks garbled.
- A rare hang on a "Now Loading" screen (much less frequent since disc reads were reworked).

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
- [SDL2](https://www.libsdl.org/) and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): the PC version.
- [stb_easy_font](https://github.com/nothings/stb) (public domain / MIT): the settings menu's text.
- gamemasterplc's 16:9 Gecko code: the idea behind the widescreen option.

Sonic Riders is © SEGA. This project is not affiliated with SEGA.
