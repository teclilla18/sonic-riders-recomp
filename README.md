# UPDATES PAUSED UNTIL 11/19 Enjoy!! (working on online play)
> ☕ **Enjoying the port? [Support it on Ko-fi](https://ko-fi.com/teclilla18)**: every coffee helps keep updates coming.
>
> **Vote: what comes next?** Online play or custom characters and stages: [cast your vote in the poll](https://github.com/teclilla18/sonic-riders-recomp/discussions/26)
>
> **Alpha testers needed:** [Sonic Heroes native port](https://github.com/teclilla18/sonic-heroes-recomp)
>
> **New here? [Easy-read install guide](https://claude.ai/artifact/4PSHvwKrxWKTqAp5tDCu6W)**: step by step for Switch, PC,
> Android, Linux / Steam Deck and iPhone / iPad. An `.iso` or Dolphin's `.rvz` both work (v0.8.4 converts RVZ by itself).
>
> **Updating got revamped in v0.8.3.** Once you're on v0.8.3, every future update is one press: **UPDATE NOW** in
> the app downloads, rebuilds and installs it by itself, usually in under a minute. Switch players can update
> without taking the SD card out (hekate's USB mode). [Easy-read update guide](https://claude.ai/artifact/FtNxFJFBdtLqmMFW7QjXFN) · [How to update](docs/INSTALL.md#updating-to-a-new-version)

<p align="center">
  <img src="docs/images/banner.jpg" alt="Sonic, Tails, Knuckles, Jet, Wave and Storm racing on Extreme Gear through a neon city" width="100%">
</p>

# Sonic Riders Recomp: native port for PC, Switch, Android & iOS

<p align="center">
  <a href="https://decompbuddy.com/games/sonic-riders-gamecube"><img src="https://decompbuddy.com/badge.svg" alt="Listed on Decomp Buddy"></a>
  <a href="https://decompbuddy.com/games/sonic-riders-gamecube?install"><img src="https://decompbuddy.com/install-button.svg" alt="Install With Decomp Buddy"></a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MPL--2.0-blue" alt="License: MPL-2.0"></a>
  <img src="https://img.shields.io/badge/Focus-Game%20Preservation-pink" alt="Focus: game preservation">
  <img src="https://img.shields.io/badge/Platform-PC%20%7C%20Switch%20%7C%20Android%20%7C%20Linux%20%7C%20iOS-green" alt="Platforms: PC, Switch, Android, Linux, iOS">
  <img src="https://img.shields.io/badge/Recompiler-gcrecomp-orange" alt="Recompiler: gcrecomp (this repo)">
  <img src="https://img.shields.io/badge/Type-Static%20Recompilation-blueviolet" alt="Type: static recompilation">
  <img src="https://img.shields.io/badge/Status-Playable-brightgreen" alt="Status: playable">
  <a href="https://github.com/teclilla18/sonic-riders-recomp/releases/latest"><img src="https://img.shields.io/github/v/release/teclilla18/sonic-riders-recomp?label=Release&color=0aa5e8" alt="Latest release"></a>
  <a href="https://github.com/teclilla18/sonic-riders-recomp/releases"><img src="https://img.shields.io/github/downloads/teclilla18/sonic-riders-recomp/total?label=Downloads&color=teal" alt="Downloads"></a>
  <a href="https://github.com/teclilla18/sonic-riders-recomp/stargazers"><img src="https://img.shields.io/github/stars/teclilla18/sonic-riders-recomp?style=flat&logo=github&color=yellow" alt="GitHub stars"></a>
  <a href="https://ko-fi.com/teclilla18"><img src="https://img.shields.io/badge/Support%20on-Ko--fi-FF5E5B?logo=ko-fi&logoColor=white" alt="Support on Ko-fi"></a>
</p>

Play **Sonic Riders** (GameCube, USA) natively, with no emulator, on a modded **Nintendo Switch**, a
**Windows PC**, an **Android phone**, **Linux / Steam Deck** (new, untested on a Deck), or an **iPhone / iPad** (new, built on a Mac). The game is rebuilt on your own PC from **your own disc image**; this project contains
no game files.

## Riders Plus: Sonic Riders, turned up

<p align="center">
  <img src="docs/images/v0.8.2/riders-plus-banner.jpg" width="100%" alt="Sonic on the Blue Star racing through a neon city under the Riders Plus logo, with 999 rings and a Lv. 5 badge">
</p>

**Riders Plus** is our own take on the game's racing. It's built into this port and **on by default**. Every
track, character and gear stays the same; the limits are what change. You get more rings, two more gear levels,
and you keep your speed instead of losing it. Turn it off any time in **Settings > Gameplay > Riders Plus** for
the original GameCube rules. Riders Plus and [online play](docs/ONLINE.md) are the two parts of this project
we'll keep building on.

| | Original game | Riders Plus |
|---|---|---|
| Ring limit | 100 | **999** (a hit or a fall still costs 100) |
| Gear levels | Lv. 1-3 | **Lv. 4 at 200 rings, Lv. 5 at 350** |
| Gold 100-ring capsule | Gone once taken | **Comes back after 25 seconds** |
| Dash panels | Set you to the panel's speed, even if you were faster | **The panel's speed, or your speed + 25 km/h** |
| Speed Shoes | Set you to 200 km/h | **Your speed + 80 km/h** (at least 200) |
| Charging a jump | Slows you down at once | **Keeps your speed for 0.75 s** |

<p align="center">
  <img src="docs/images/riders-plus/lv4-level-up.jpg" width="49%" alt="LEVEL UP!! to Lv. 4 at 203 rings, Metal City, 250 km/h">
  <img src="docs/images/riders-plus/lv5-level-up.jpg" width="49%" alt="LEVEL UP!! to Lv. 5 at 350 rings, Metal City">
  <br><sub>LEVEL UP!! to Lv. 4 at 200 rings, and to Lv. 5 at 350</sub>
</p>

### Lv. 4 and Lv. 5

Collect 200 rings for Lv. 4 and 350 for Lv. 5. You get the game's own LEVEL UP!! (sparkle, sound and a full air
tank), and the HUD shows "Lv. 4" or "Lv. 5" in the game's own digits. Each gear keeps its own Lv. 1-3 stats and
builds on its Lv. 3 numbers. Sonic's Blue Star goes from 180 / 250 km/h (top speed / boost) at Lv. 3 to 200 / 275
at Lv. 4 and 215 / 300 at Lv. 5.

<p align="center">
  <img src="docs/images/gear-levels-chart.svg" width="49%" alt="Chart: Blue Star top speed and boost from Lv. 1 to Lv. 5">
  <img src="docs/images/v0.8.2/charge-jump-chart.svg" width="49%" alt="Chart: speed while charging a jump, original vs Riders Plus">
</p>

### More rings, more speed

<p align="center">
  <img src="docs/images/riders-plus/gold-capsule-298.jpg" width="100%" alt="1st place at Lv. 5 with 402 rings, 298 km/h, a gold 100-ring capsule beside the track">
  <br><sub>1st place, 402 rings, 298 km/h, with the gold 100-ring capsule back on the track</sub>
</p>
<p align="center">
  <img src="docs/images/riders-plus/lv5-boost.jpg" width="49%" alt="Boosting at Lv. 5 with 433 rings, 293 km/h">
  <img src="docs/images/riders-plus/tunnel-294.jpg" width="49%" alt="Metal City tunnel at 294 km/h">
  <br>
  <img src="docs/images/riders-plus/trick-rank-ss.jpg" width="49%" alt="Rank SS trick in Metal City">
  <br><sub>Lv. 5 boosts at 293 and 294 km/h, and a Rank SS trick. All shots: a real race on PC, 21:9 (3440 x 1440)</sub>
</p>

### What's next

Riders Plus gets new additions with each release. Next up: extra character slots for mod characters, custom music
([#17](https://github.com/teclilla18/sonic-riders-recomp/discussions/17)) and a right-stick camera. Have an idea for
it? [Tell us in Discussions](https://github.com/teclilla18/sonic-riders-recomp/discussions).

<sub>Riders Plus banner: made by the project owner with ChatGPT. Race shots: this port on PC. Charts: our own, from
measured PC races.</sub>

**Original** (4:3)
<a href="docs/images/compare/original-1.jpg"><img src="docs/images/compare/original-1.jpg" width="100%" alt="Metal City race start in the original game, 4:3 with black bars, 30 fps"></a>
**This port** (PC, 21:9)
<a href="docs/images/compare/recomp-1.jpg"><img src="docs/images/compare/recomp-1.jpg" width="100%" alt="The same race start in this port, ultrawide"></a>

**Original** (4:3)
<a href="docs/images/compare/original-2.jpg"><img src="docs/images/compare/original-2.jpg" width="100%" alt="Metal City stage intro in the original game, 4:3"></a>
**This port** (PC, 21:9)
<a href="docs/images/compare/recomp-2.jpg"><img src="docs/images/compare/recomp-2.jpg" width="100%" alt="The same Metal City intro in this port, ultrawide"></a>

> [!IMPORTANT]
> **Switch:** no overclock needed for a steady 30 fps: at stock clocks races run at 30 fps, and
> the *60 fps* setting gives 60 in lighter parts of a race and 30 in the busiest ones. A steady 60 still
> needs an overclock (tested at CPU 1785 MHz / GPU 1152 MHz / memory 2133 MHz on an OLED; overclock at
> your own risk).

## Get it

| I want to play on... | Download from the [latest release](../../releases/latest) | Guide |
|---|---|---|
| a **Windows PC** | `SonicRiders-PC-Setup-<version>.zip` | [Install on PC](docs/INSTALL.md#install-on-pc) |
| a modded **Nintendo Switch** | `SonicRiders-Switch-Setup-<version>.zip` | [Install on Switch](docs/INSTALL.md#install-on-switch) |
| an **Android phone** (64-bit, Android 11+) | `SonicRiders-Android-Setup-<version>.zip` | [Install on Android](docs/INSTALL.md#install-on-android) |
| **Linux** or a **Steam Deck** (new, test version) | `SonicRiders-Linux-Setup-<version>.zip` from the [Linux test pre-release](../../releases/tag/v0.5.0-linux-test) | [Install on Linux / Steam Deck](docs/INSTALL.md#install-on-linux--steam-deck) |
| an **iPhone** or **iPad** (new; built on a Mac) | `SonicRiders-iOS-Setup-<version>.zip` | [Install on iPhone / iPad](docs/INSTALL.md#install-on-iphone--ipad) |

**In short:** extract the zip, double-click the `Install Sonic Riders` file, pick your disc image, and
wait 10–30 minutes the first time (Android: 20–40, then it installs over a USB cable). Every version is
built on a Windows 10/11 PC, except Linux / Steam Deck, which builds on the Linux machine itself
(no compiler or root rights needed), and iPhone / iPad, which builds on a Mac with Xcode.

**You need:** your own **Sonic Riders (USA)** disc image (`GXEE8P`: a full 1.4 GB `.iso`, or Dolphin's `.rvz`, which the setup converts to `.iso` by itself) and a
64-bit Windows 10 (1803+) or 11 PC with internet the first time. Switch: custom firmware with the
Homebrew Menu. PC: a graphics card with OpenGL 3.3. Android: USB debugging turned on, about 2 GB free on
the phone. [All requirements](docs/INSTALL.md#what-you-need)

## Playing

| | Switch | PC keyboard | PC gamepad |
|---|---|---|---|
| Move | Left stick | W A S D | Left stick |
| A / B / X / Y | A / B / X / Y | J / K / L / I | A / B / X / Y |
| START | + | Enter | Start |
| Settings menu | hold − | hold F1 | hold Back |

Android: touch controls on screen (top-left button = settings menu) or any controller. Up to 4 players on
Switch and PC, 2 on one phone: connect more controllers. [Full controls, settings and multiplayer](docs/PLAYING.md)

## Status

<p align="center"><img src="docs/images/progress.svg" width="720" alt="Road to v1.0: 70% done"></p>

<details>
<summary><b>What v1.0 means, and what each bar still needs</b></summary>

v1.0 = the whole game plays like the GameCube original on every supported platform, Switch races hold their
frame rate, and you can race friends online. The overall number is a weighted average (game 25 %, the
rest 15 % each), judged by hand at each release; `tools/make_progress.py` draws the card.

| Area | Done | Left for v1.0 |
|---|---|---|
| Whole game playable | 90 % | Grand Prix freeze after character select (#18), rare "Now Loading" hang (#6) |
| Graphics match the GameCube | 80 % | Lens flare (#7), story cutscene monitor (#4), stronger speed blur (#3) |
| Switch performance | 70 % | 60 fps at stock clocks (#1), 3-4 player races at 60 with an overclock (#2) (done on main: heavy 1P views hold 60 at 1.5x with picture options, overclocked) |
| Platforms | 85 % | Linux / Steam Deck out of test, Switch Lite / original / docked reports (#9) |
| Online play | 55 % | PC-PC, PC-Switch and Switch-Switch races stay in sync (tested over home networks); each player keeps their own screen shape; still to do: internet play (lobby server with room codes), a room-wide 30 fps switch for slow consoles, phones, 3-4 players, ping and delay settings |
| Features and polish | 67 % | Custom music, right-stick camera, code mods, new characters from mods (done: the "Riders Plus" set in v0.8.2; one-press in-app updates in v0.8.3, its download step not yet run for real; Dolphin-format texture packs and easier stat mods on main, tested on PC) |

</details>

Playable from boot to races, story mode and 4-player Free Race, with sound, on both platforms.

- **PC:** 60 fps on the test PC (RTX 3060), 4 players included.
- **Switch (stock clocks):** a steady 30 fps in races (1 player tested); 60 fps in lighter scenes with
  the *60 fps* setting.
- **Android (new in v0.4.0):** 60 fps races on a Galaxy S23 Ultra (other phones untested).
  **Screen recording is not supported on Android:** recording the screen during a race makes the phone's
  graphics driver reset the game again and again until it closes. Record with a capture card or play
  without recording.
- **iPhone / iPad (new):** 60 fps races on an iPhone 17 Pro, iPad Pro 11" (M1) and iPad (A16) (a short stutter
  the first time a new effect appears); the iPad 7th generation (2019) plays with small dips on busy tracks.
- **Switch (overclocked):** 1–2 players mostly 60 fps; 3 players mostly 60 with dips in the busiest
  moments; 4 players about 30 fps.
- Known issues are tracked in [Issues](../../issues); what changed in each version is in the
  [CHANGELOG](CHANGELOG.md).

## More

| | |
|---|---|
| [Installing, updating, troubleshooting](docs/INSTALL.md) | Requirements, step-by-step setup, fixes for common problems |
| [Playing](docs/PLAYING.md) | Controls, settings menu, multiplayer, debug options |
| [Reporting bugs](docs/REPORTING-BUGS.md) | What to include, logs, screenshots |
| [Modding](docs/MODDING.md) | Swapping characters, changing stats, custom tracks (PC, in progress) |
| [Building from source](docs/BUILDING.md) | For developers |
| [Contributing](CONTRIBUTING.md) | Help is welcome from players and developers; coding agents can use [CLAUDE.md](CLAUDE.md) |

## Support development

Enjoying Sonic Riders Recompiled? Consider supporting continued development!

☕ [Support the project on Ko-fi](https://ko-fi.com/teclilla18)

Donations are completely optional and do not grant ownership or rights to any Sonic Riders intellectual property.

## How it works

The game's PowerPC code (`main.dol` + `_Main.rel`) is translated to C ahead of time on your PC
(**static recompilation**), and the GameCube hardware (graphics, disc, audio, memory card,
controllers) is reimplemented natively: deko3d on the Switch, OpenGL 3.3 on PC. The approach follows
[SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD) and
[ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp), whose PowerPC→C translator this
project forks. Current status is in [STATE.md](STATE.md); design notes in [docs/REFERENCE.md](docs/REFERENCE.md) and [docs/HISTORY.md](docs/HISTORY.md).

## Legal

- A fan project, **not affiliated with, endorsed or sponsored by SEGA or Nintendo**. "Sonic Riders"
  and "Nintendo Switch" are trademarks of their owners, used only to describe what this works with.
- **This repository and its releases contain no game code, assets, data or console keys.** You need
  your own disc image of Sonic Riders (USA), made from a disc you own. Everything derived from the
  game is generated locally on your PC by the setup tool.
- **Never share** your built `.nro`, `.exe` or `.nsp`, or your disc image. Issues and pull requests
  that contain game files are deleted.

Licensed under the [Mozilla Public License 2.0](LICENSE), like the ZeldaWWHDRecomp translator it
forks (`third_party/LICENSE-MPL-2.0`).

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
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT): the settings menu; its layout follows [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD)'s settings overlay.
- [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) (MIT): the
  sharpening and upscaling (EASU, RCAS), ported to GLSL in `runtime/src/platform/host/fsr1_gl.h` and
  `runtime/shaders/vk_easu.frag` / `vk_rcas.frag`.
- gamemasterplc's 16:9 Gecko code: the idea behind the widescreen option.
- Banner image: fan art by an unknown artist. If it's yours, open an issue and we'll credit you or remove it.
- Game icon (`setup/icon.png`): fan-made Sonic Riders icon by [cedry2kio](https://www.deviantart.com/cedry2kio) on
  DeviantArt, used with credit. cedry2kio, if you'd like it changed or removed, open an issue and we'll do it right away.

Sonic Riders is © SEGA. This project is not affiliated with SEGA.
