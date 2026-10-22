# Instructions for coding agents (Claude Code and others)

This is a static-recompilation port of Sonic Riders (GameCube, USA, GXEE8P rev 0) to the Nintendo
Switch. The game's PowerPC code is translated to C on the contributor's own machine from their own
disc image; this repository holds only our own source. Human contributors: see CONTRIBUTING.md.

## Hard rules

1. **Never commit, paste or upload anything derived from the game**: disc images, `baserom/`,
   `build/` (recompiled C in `build/gen/`, objects, `.nro`/`.exe`/`.nsp`), decomp-toolkit output
   (`config/*/symbols.txt`, `splits.txt`), disassembly, extracted assets, shader keys, saves, console
   keys. Small facts are fine (addresses, function names, register values, short descriptions of
   behaviour); blocks of disassembly or recompiled code are not.
2. Run `python tools/check_no_game_data.py` before committing. The pre-commit hook
   (`git config core.hooksPath tools/git-hooks`) and CI run it too. If it refuses a file, fix the
   file; never bypass the hook (`--no-verify`) or weaken the check to get a commit through.
3. No personal details in tracked files: no home-folder paths, SD-card drive letters, console
   setup, emails. Write `<repo>`, `<home>`, `<sd>`. Machine-specific notes go in `NOTES.local.md`
   (gitignored).
4. Never modify the user's original disc image. Work on copies.

## Orientation

- **NOTES.md** is the design log and the place to resume from: architecture, decisions and why,
  debug variables (`SR_*`), findings, open items ("STATE" and "Open items" near the end). Read the
  section for the area you touch before changing it, and append what you learn (what was wrong, how
  it was found, the fix, what is still unproven). Mark claims [confirmed] / [likely] / [uncertain].
- `tools/recomp/` recompiler (PowerPC → C), `config/GXEE8P/` its inputs (`hle.txt` native
  replacements, `patches.txt` code patches); `runtime/src/` hardware models and the runtime;
  `runtime/src/platform/{switch,host}/` per-target code; `runtime/shaders/` the shared GX shader.

## Build and test

- First time: `setup\setup.ps1 -Iso <image.iso> -Target both -NoSd -Yes` (Windows). It downloads a
  pinned toolchain to `%LOCALAPPDATA%\SonicRidersSetup\tools` and builds `build/switch/sonicriders.nro`
  and `build/host-mingw/sonicriders.exe`. After that: `python configure.py --target host --toolchain
  mingw --llvm-mingw <tools>\llvm-mingw --sdl <tools>\sdl2` (or `--target switch --devkitpro
  <tools>\devkitpro`) and `ninja`.
- **The PC build is the fast test loop.** Useful variables: `SR_MINIMIZED=1` (window stays in the
  background; the user may be using the PC), `SR_NOAUDIO=1`, `SR_SCREENSHOT=<frame>` (saves
  `build/shot_<frame>.png`; look at it), `SR_INPUT="1500:START,1510:-START,..."` (scripted buttons
  at VI field counts), `SR_PADS=N`, `SR_WATCHDOG=<s>`, `SR_PROFILE=1`. The log goes to stderr.
- Don't screen-capture the desktop; use the port's own screenshots.
- Switch behaviour (performance especially) can only be confirmed on hardware. Say so in the pull
  request instead of claiming it. Performance on hardware so far assumes an overclocked console
  (CPU 1785 / GPU 1152 / memory 2133 MHz); stock-clock performance is an open problem.
- GLSL rule (deko3d's uam compiler): never index a vector or a local array with a runtime value; use
  the explicit select helpers in `gx_common.glsl` / `gx.frag`.
- Every host helper thread on the Switch must be pinned off core 0 (the game thread's core).

## Pull requests

Keep each PR to one topic. Describe the change, why, and how it was tested (PC route and frames,
emulator, hardware), with `[perf]` numbers for performance work. Update NOTES.md and, for user-visible
changes, CHANGELOG.md. End commit messages with the usual co-author line for your agent if you use one.
