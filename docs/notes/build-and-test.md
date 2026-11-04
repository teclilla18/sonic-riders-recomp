# Build, test and tools

**Reference.** Moved from docs/REFERENCE.md on 2026-11-03 (content unchanged). Index: [REFERENCE.md](../REFERENCE.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## Quick commands (copy-paste; `<tools>` = %LOCALAPPDATA%/SonicRidersSetup/tools)

build.ninja holds one target at a time: configure, then ninja. From Git Bash use `<tools>/ninja/ninja.exe` (the
`ninja` on PATH is devkitPro's), `T=$(cygpath -m "$LOCALAPPDATA/SonicRidersSetup/tools")`.
- Android: `python configure.py --target android --android-sdk $T/android-sdk --jdk $T/jdk17 --sdl-src $T/sdl2-src
  --abi arm64-v8a --keystore $T/android/sonicriders-local.keystore [--icon <file>]` → `build/android/sonicriders-arm64-v8a.apk`.
- Switch: `DEVKITPRO=$T/devkitpro python configure.py --target switch --devkitpro $T/devkitpro --imgui $T/imgui`
  (keep `DEVKITPRO` exported for ninja) → `build/switch/sonicriders.nro`.
- PC: `python configure.py --target host --toolchain mingw --llvm-mingw build/ext/llvm-mingw-20261006-ucrt-x86_64
  --sdl build/ext/SDL2-2.32.10-mingw` → `build/host-mingw/sonicriders.exe`. A PC run logs to **stdout** (redirect
  it); `sonicriders.log` in the root is not written by test runs started this way.
- Phone run: `MSYS_NO_PATHCONV=1 python tools/android_run.py --apk build/android/sonicriders-arm64-v8a.apk --package
  net.srnative.sonicriders --args "SR_..." --wait "<regex>" --pull <file> --out C:/.../build/android/run/<name>
  [--keep-running] > build/android/run/<name>.txt` (Windows paths as `C:/...` with MSYS_NO_PATHCONV). Screen
  3088x1440; touch layout in units of H = 1440; the menu's 16:9 rect is x 264-2824 (overlay px x2 + 264).
- Synthetic touch on the phone: `adb shell input tap X Y` / `input swipe X0 Y0 X1 Y1 MS`; menu button (202, 122).
- `SR_RAMHASH=<N>` is a **step limit** (hashing and `SR_RAMDUMPAT` dumps happen only for steps < N); with
  `SR_RAMHASH_BLOCKS=none SR_RAMHASH_KEY=<addr>` it logs one u32 per step cheaply (watch a variable over a run).
- Find a game variable: dumps at known steps (`SR_RAMDUMPAT`), keep words equal within each group and different
  between groups (e.g. menus vs races). Det-route steps (`SR_PERFTEST=1 SR_DETCLOCK=1 SR_FRAMERATE=0`): intro ~1500,
  title ~2900, 1P demo race 3304-7183 (e.g. 5300), title 8000, 2P demo race from 9013 (e.g. 10500).
- PC menu route from an empty card (`SR_CARD=<new file>`): START at 300/600/900/1200, A every 250 from 1500 to 6500
  → Player Entry; then START + A at 6800/7100/... → Free Race at Metal City (mode 8 from step ~6825).

## Switch: build, install and test

Older manual steps (the setup tool and `configure.py --target switch --devkitpro <tools>\devkitpro` are the current way, see CLAUDE.md and BUILDING.md):

**Switch: build, install and test**
- Build:
  ```
  $env:DEVKITPRO="C:/devkitPro"
  python configure.py --target switch
  ninja
  ```
  The result is `build/switch/sonicriders.nro`. It contains recompiled game code: never share or commit it.
- SD card layout (`sdmc:/switch/sonicriders/`):
  - `sonicriders.nro`
  - a **copy** of the user's disc image (any `*.iso`, GXEE8P rev 0)
  - created at runtime: `saves/card_a.raw` (memory card) and `sonicriders.log`
- Launch from the Homebrew Menu **in title mode** (hold R while starting any installed game). The guest memory window prefers `svcMapProcessCodeMemory`; without it, it falls back to `svcMapMemory`, and the log's `[mem]` line says which was used. Applet mode is untested and has much less memory.
- Controls: A B X Y = A B X Y, + = START, ZL/ZR = L/R, R = Z, sticks = main/C, D-pad. HOME exits. Hold − (Minus) for the settings menu.
- The log's `[perf]` lines give fps every 120 frames, plus a ms/frame breakdown. `[deko3d]` lines give render-thread and GPU ms/frame. `shot_<frame>.bmp` files (frames 300 … 7200) are self-captures. `env.txt` sets debug variables.

## Host (Windows): build and run

Original clang + MSVC method (the setup's llvm-mingw build is the current way, see CLAUDE.md):

**How to build/run (host, Windows):**
```
build/tools/dtk.exe dol split config/GXEE8P/config.yml build/GXEE8P   # once (analysis; writes config/GXEE8P/*symbols.txt)
python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen        # recompile (also run by ninja)
. tools/host/env.ps1                                                   # PowerShell: clang + MSVC/SDK paths (vcvars64.bat fails on this PATH)
python configure.py ; ninja                                            # ninja: %APPDATA%\Python\Python312\Scripts\ninja.exe
build/host/sonicriders.exe "baserom/Sonic Riders (USA) (En,Ja,Fr,De,Es,It).iso"
```
After a fresh clone, run `git config core.hooksPath tools/git-hooks` (the game-data guard).

## Setup tool, releases, PC icon

**Setup tool and releases (2026-10-22):** `SonicRidersSetup.bat` → `setup/setup.ps1` builds everything from the user's disc image with a SHA-256-pinned toolchain (`setup/toolchain.json`; downloads through Windows' `curl.exe`, since devkitPro's server refuses .NET's web client even with the pacman user agent). PC builds from it use llvm-mingw (`configure.py --toolchain mingw`, output `build/host-mingw/`). `tools/make_release.py --version X` zips the files tracked at HEAD (guard-checked) plus a `VERSION` file; the port logs `[build] <version or git describe>` first (`build/<target>/gen/version.c`, written by configure.py). Hardware performance so far is with an overclock (CPU 1785 / GPU 1152 / MEM 2133); stock clocks run poorly.

**PC program icon (2026-10-23):** [confirmed] `tools/make_icon.py` reads the memory card save icon (32x32 CI8, 8x4 tiles; RGB5A3 palette) from the verified `_Main.rel` at 0x1F7660 (palette at 0x1F7420; found by matching the icon in a real save in `saves/card_a.raw`), writes `build/icon.ico` (PNG entries 16/32/64/128/256: 2x2 average for 16, nearest 2x/4x/8x above) and `configure.py` compiles it in with llvm-mingw's windres when the file exists. SDL2 takes the window/taskbar icon from the program's first icon resource. The setup makes it after step 4 for the PC target (failure only warns). The disc banner (`opening.bnr`, 96x32 logo only) was too wide to read as a square icon.

## GitHub and pushing

**GitHub:** https://github.com/teclilla18/sonic-riders-recomp (public). Push at the end of every work session, after `python tools/check_no_game_data.py` passes.
- **Push at the end of every work session.**
- Before pushing, check `git status --ignored`: no ISO, DOL, REL, assets, disassembly, recompiled output or dtk symbol files.
- If anything questionable would be included, stop and ask the user.

## Testing tools: Eden, Dolphin, test scripts

**Eden** (Switch emulator): the original notes follow below ("Testing without hardware"). Also: start it with
`-WindowStyle Hidden`, never minimized (zero-size surface: divide-by-zero crash / rendering stalls); it is muted
(`user/config/sdl2-config.ini`); SR_INPUT now works there too through env.txt; GPU waits at new scenes are Eden
compiling shaders, and its numbers (JIT) are indicative only.

**Dolphin** (reference for how the real game looks):
- Dolphin 2609a, official build, portable in `build/ext/dolphin` (gitignored).
- `Dolphin.exe -b -e <iso>` with no input, unthrottled, frame dump (AVI; ffmpeg from the imageio-ffmpeg package
  reads it even unfinalized): the attract-mode demo races are the reference (Metal City race at Dolphin frames
  ~8800-12300). Runs are not frame-deterministic: compare by averages or the race timer.
- Hand-made `.dtm` input movies are ignored. For menus, `tools/dolphin_dsu_input.py` is a DSU (cemuhook) server that
  Dolphin reads as a controller (device `DSUClient/0/Script`; Start = Options, A = Cross; face buttons need the
  analog bytes). Player Entry with Sonic at Dolphin frame ~2590 from an empty card.
- `SR_DFF` frames play in Dolphin with `Dolphin.exe -b -e frame.dff`; needs `Core.RAMOverrideEnable=True`,
  `Core.MEM1Size=29360128` (our REL sits above 24 MB).
- `GFX.Settings.DumpTextures` via `-C` dumped nothing.

**Hardware over Wi-Fi:** the switch-remote MCP (buttons, screenshots, FTP, nxlink). The game's log is locked while
it runs (FTP read fails), and a read held open at launch made the game skip its log and env.txt. HOME tiles wrap; the
game tile asks "Who is using the software?" first; presses can be dropped, so check with a screenshot.
Console-specific details are in NOTES.local.md.

**Test routes and scripts (scratchpad, not committed; recreate if missing):** from the 2026-10-19 STATE notes:

**Test routes (scratchpad, recreate if missing):** route1p.txt (race), route2p/route4p.txt (Free Race 2/4 players, SR_PADS), routestory.txt / routestory2.txt (story, A presses through cutscenes), routestory3.txt (story, START skips); mp.ps1 -Keys -Shots -Secs -Pads [-Watchdog] (sets SR_NOAUDIO=1, SR_MINIMIZED=1); loop4p.ps1; cmpadx.py + adx.py (ADX decode and correlation against a WAV).

**Dolphin reference status:** installed and works for attract-mode demos (no input needed). Input movies (-m) do NOT take effect in 2609a here; if menu comparisons are needed, try a DSU (cemuhook UDP) fake controller with BackgroundInput.

Useful PC routes: the user's save route (START 1500/1800/2100, A 2450/2700/3050/3400; menu input from ~field 3930);
Player Entry (START at 300/600/900/1200, A at 1500..3300 every 250-300 fields); the 2P online route (mk2p.py, see
docs/ONLINE.md phase 3).
