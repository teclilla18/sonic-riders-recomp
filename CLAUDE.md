# Instructions for coding agents (Claude Code and others)

Goal: play Sonic Riders (GameCube, USA, GXEE8P rev 0) natively on a modded Switch (and on Windows) by static
recompilation: the PowerPC code becomes C on each contributor's machine from their own disc; this repo holds only our source.

## Hard rules

1. **Game-data guard.** Never commit, paste or upload anything derived from the game: ISO/disc images, DOL, REL,
   `baserom/`, extracted assets, disassembly, recompiled output (`build/`, `build/gen/`), `.nro`/`.nsp`/`.exe`,
   dtk output (`config/*/symbols.txt`, `splits.txt`), shader keys, saves, console keys. Small facts (addresses,
   function names, register values) are fine; blocks of disassembly or recompiled code are not.
2. Before every commit and push run `python tools/check_no_game_data.py` and check `git status --ignored`. If
   anything questionable would be included, **stop and ask the user**. Never `--no-verify` or weaken the check.
3. **Push to GitHub at the end of every session**, after rule 2 passes.
4. No personal details in tracked files (home paths, SD drive letters, console setup, emails): write `<repo>`,
   `<home>`, `<sd>`. Machine-specific notes go in `NOTES.local.md` (gitignored; read it if present).
5. Never modify the user's original disc image. Work on copies.

## Hard-won lessons

- Shared GLSL (deko3d's uam compiler miscompiles): never index a vector or a local array with a runtime value (use
  `pick()`, `comp()`, `cinput()`, `ainput()`, `reg()`); in vertex shaders no loop-carried vec4 accumulator seeded
  from a parameter and no `continue` in vector-accumulating loops. After shader changes every dksh in
  `build/switch/shaders/` must report `per_warp_scratch 0`.
- Every host helper thread on the Switch is pinned off core 0 (the game thread). Horizon does not time-slice equal
  priorities: a thread that must keep up (the audio mixer) runs one step above its core neighbour.
- Eden: v0.1.1 only (v0.2.1 fails in dkDeviceCreate); never start it minimized (use `-WindowStyle Hidden`).
- Look at frames through the port's own captures (`SR_SCREENSHOT`, `shot_*.bmp`, switch-remote screenshots), never
  desktop screenshots. PC test runs use `SR_MINIMIZED=1` (the user may be using the PC).
- Switch CPU gains can't be measured on PC or Eden; only hardware. The test console is overclocked (CPU 1785 /
  GPU 1152 / MEM 2133); state the clocks with every hardware number. Hardware `[profile]` charges a caller's work
  after a call to the callee.
- In hardware logs check `[deko3d]` "specialized shaders for N of M states": a fallback to the ubershader silently
  multiplies GPU time (shader keys must contain only bits the shader reads).

## Where things are (open only what the task needs; grep, don't read whole files)

- **STATE.md**: read at the start of every session (status, open items, next hardware test).
- **docs/REFERENCE.md**: architecture, HLE/device boundary, build/install/test, `SR_*` and `RECOMP_*` variables,
  Eden/Dolphin, game addresses, SDK map, recon. `grep -n '^## ' docs/REFERENCE.md`, then read that section.
- **docs/HISTORY.md**: every dated session and hardware test, oldest first. Grep a date, `#issue`, address or `SR_`.
- docs/ONLINE.md (online play), docs/PERF.md (perf test), docs/MODDING.md, docs/BUILDING.md, CONTRIBUTING.md.
- Code: `tools/recomp/` recompiler, `config/GXEE8P/` (`hle.txt`, `patches.txt`), `runtime/src/` devices and runtime,
  `runtime/src/platform/{switch,host}/`, `runtime/shaders/` shared GX shader.

## Build and test (the PC build is the fast test loop)

- First time: `setup\setup.ps1 -Iso <image.iso> -Target both -NoSd -Yes`. Then `python configure.py --target host
  --toolchain mingw --llvm-mingw <tools>\llvm-mingw --sdl <tools>\sdl2` (or `--target switch --devkitpro
  <tools>\devkitpro`), `--imgui <tools>\imgui` once, and `ninja`. Variables: REFERENCE.md "Debug variables".

## End of every session

1. Rewrite STATE.md in place (never append; under ~120 lines; newest facts only).
2. Add a short dated entry at the end of docs/HISTORY.md: what was wrong, how it was found, the fix, what is
   unproven; mark [confirmed] / [likely] / [uncertain]. Durable facts (addresses, variables, procedures) also go
   into docs/REFERENCE.md. User-visible changes go in CHANGELOG.md.
3. Guard + `git status --ignored`, commit, push. PRs: one topic; say what changed, why and how it was tested (PC
   route and frames, emulator, hardware, `[perf]` numbers); end commit messages with your agent's co-author line.
