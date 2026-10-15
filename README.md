# Sonic Riders native port (work in progress)

An unofficial effort to run Sonic Riders (GameCube, USA, `GXEE8P` rev 0) natively on a
modded Nintendo Switch through **static recompilation**:
- the game's PowerPC code (main.dol + _Main.rel) is translated to C ahead of time;
- the Dolphin SDK's system libraries are reimplemented natively;
- graphics are drawn with deko3d.

The approach follows [SwitchWakerHD](https://github.com/centollOS/SwitchWakerHD) and its upstream
[ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp), whose PowerPC→C translator
this project forks (MPL-2.0, see `third_party/`).

**This repository contains no game code, assets or data.** Everything derived from the game
(disassembly, recompiled C, builds) is generated locally from your own legally obtained disc
image and stays out of git (`.gitignore`, plus the `tools/git-hooks/pre-commit` guard:
run `git config core.hooksPath tools/git-hooks` after cloning).

Status, design decisions and build steps: [NOTES.md](NOTES.md).
