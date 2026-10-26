# Building from source

[← Back to the README](../README.md)

The setup tool *is* the build: `SonicRidersSetup.bat` (it asks Switch or PC) works the same from a
`git clone`. For non-interactive use:

```
powershell -ExecutionPolicy Bypass -File setup\setup.ps1 -Iso <image.iso> -Target switch|pc|both [-Sd E:\ | -NoSd] [-Yes]
```

The steps it runs, if you want to do them by hand (tools on `PATH`, devkitPro in `DEVKITPRO`):

```
python tools/extract_iso.py <image.iso> baserom                        # main.dol, _Main.rel (hash-checked)
dtk dol split config/GXEE8P/config.yml build/GXEE8P                    # decomp-toolkit 1.8.4 analysis
python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen        # PowerPC -> C
# Dear ImGui v1.92.9b (the settings menu): https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip
# extracted to build/ext/imgui-1.92.9b, or pass --imgui <dir> to configure.py (the setup downloads it)
python configure.py --target switch --devkitpro <devkitPro dir>        # -> build/switch/sonicriders.nro
python configure.py --target host --toolchain mingw --llvm-mingw <dir> --sdl <SDL2 mingw dir>   # -> build/host-mingw/
ninja
```

The PC build plays the first `*.iso` in `baserom/` (or the image given on its command line).

After cloning, enable the game-data guard: `git config core.hooksPath tools/git-hooks`. It refuses
commits that contain anything derived from the game.

Where to read next:

- [NOTES.md](../NOTES.md): the design log (architecture, decisions, findings, debug variables).
- [CONTRIBUTING.md](../CONTRIBUTING.md): how to contribute, test and send a pull request.
- [CLAUDE.md](../CLAUDE.md): the rules and test workflow for coding agents.
