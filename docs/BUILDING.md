# Building from source
**Native Switch 2 Alpha:** use the [published alpha setup/source package](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.6-switch2-alpha.1) and [its guide](SWITCH2-ALPHA.md). Main does not include the experimental runtime target; do not use the original Switch build for Switch 2.

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

## macOS (Android target)

The manual steps above also build the **Android** target on macOS with a user-supplied SDK and NDK
(for example Android Studio, or `brew install android-commandlinetools`), a JDK 17 and `ninja`:

```
python configure.py --target android --abi arm64-v8a \
    --android-sdk <sdk> --jdk <jdk17> --sdl-src <SDL2 source>   # -> build/android/sonicriders-arm64-v8a.apk
ninja
```

The automatic toolchain download (`setup/setup.ps1`) stays Windows-only; here the NDK, build-tools
and JDK are supplied by the user. The NDK's host toolchain on macOS is `darwin-x86_64` (it runs on
Apple Silicon through Rosetta 2).

After cloning, enable the game-data guard: `git config core.hooksPath tools/git-hooks`. It refuses
commits that contain anything derived from the game.

Where to read next:

- [STATE.md](../STATE.md), [REFERENCE.md](REFERENCE.md) and [HISTORY.md](HISTORY.md): the design notes (status; architecture and debug variables; findings by date).
- [CONTRIBUTING.md](../CONTRIBUTING.md): how to contribute, test and send a pull request.
- [CLAUDE.md](../CLAUDE.md): the rules and test workflow for coding agents.
