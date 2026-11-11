# Instructions for coding agents (Claude Code and others)

Goal: play Sonic Riders (GameCube, USA, GXEE8P rev 0) natively on a modded Switch (and on Windows) by static
recompilation: the PowerPC code becomes C on each contributor's machine from their own disc; this repo holds only our source.

## Hard rules

1. **Game-data guard.** Never commit, paste or upload anything derived from the game: ISO/disc images, DOL, REL,
   `baserom/`, extracted assets, disassembly, recompiled output (`build/`, `build/gen/`), `.nro`/`.nsp`/`.exe`,
   dtk output (`config/*/symbols.txt`, `splits.txt`), saves, console keys. Small facts (addresses,
   function names, register values) are fine; blocks of disassembly or recompiled code are not. One exception
   (user's decision, 2026-11-07): the shader-key list `config/GXEE8P/shader_keys.txt` (GPU TEV register settings)
   is tracked, because release builds without it run Switch races at ~30 fps; other key files stay local.
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
- Switch CPU gains can't be measured on PC or Eden; only hardware. The test console (OLED) is overclocked: docked CPU 1785
  / GPU 1152 / MEM 2133, handheld CPU 1785 / GPU 614 / MEM 2133; state the mode and clocks with every hardware number. Hardware `[profile]` charges a caller's work
  after a call to the callee.
- In hardware logs check `[deko3d]` "specialized shaders for N of M states": a fallback to the ubershader silently
  multiplies GPU time (shader keys must contain only bits the shader reads).

## Where things are (open only what the task needs; grep, don't read whole files)

- **STATE.md**: read at the start of every session (status, open items, next hardware test).
- **docs/REFERENCE.md**: a short index of the topic notes in `docs/notes/` (build-and-test with **Quick commands**,
  android, debug-variables, game-addresses, architecture). Open only the note you need, grep inside it.
- **docs/HISTORY.md**: dated sessions and hardware tests from 2026-10-26, oldest first (older: `docs/archive/`).
  Grep a date, `#issue`, address or `SR_` (`grep -rn <x> docs/HISTORY.md docs/archive/`).
- **docs/MAP.md**: where each topic lives (file + function names). Use it before opening code.
- docs/ONLINE.md (online play), docs/PERF.md (perf test), docs/MODDING.md, docs/BUILDING.md, CONTRIBUTING.md.
- Code: `tools/recomp/` recompiler, `config/GXEE8P/` (`hle.txt`, `patches.txt`), `runtime/src/` devices and runtime,
  `runtime/src/platform/{switch,host}/`, `runtime/shaders/` shared GX shader.

## Status words and regressions

- Keep four states apart, in STATE.md and HISTORY.md: **implemented** (code in), **tested** (by us: say how, e.g.
  PC route, phone with adb taps, hardware + clocks), **accepted** (the user played it and said it is fine),
  **released** (in a tagged version). Evidence tags [confirmed] / [likely] / [uncertain] are about what a test proved.
- A partial test is never reported as full validation; "not reproduced" is not "fixed".
- A regression in a release: revert the change first and publish, then look for the real fix. Never stack an
  untested fix on a broken release.

## Token budget

- Search first (`grep -n`, MAP.md), then read only those lines; never read a whole file over ~200 lines.
- Cap every output (`| head`, `| tail`, `grep -c`, `cut -c1-120`); no raw logs or big directory listings.
- Long runs go to a file in the background; read only the result lines. Look at images only when the picture is
  what is being checked, shrunk or cropped first.
- Keep `build/` tidy: old test outputs go to `build/old/`. Two or three related items per session.

## Build and test (the PC build is the fast test loop)

- First time: `setup\setup.ps1 -Iso <image.iso> -Target both -NoSd -Yes`. Then `python configure.py --target host
  --toolchain mingw --llvm-mingw <tools>\llvm-mingw --sdl <tools>\sdl2` (or `--target switch --devkitpro
  <tools>\devkitpro`), `--imgui <tools>\imgui` once, and `ninja`. Variables: docs/notes/debug-variables.md.
- **Before every release: the cold Windows Sandbox test, PC and Android** (`tools/sandbox_test.py`, from the
  release zips, no download cache; ask the user before launching). Procedure: docs/notes/build-and-test.md.

## End of every session

1. Rewrite STATE.md in place (never append; under ~120 lines; newest facts only).
2. Add a short dated entry at the end of docs/HISTORY.md: what was wrong, how it was found, the fix, what is
   unproven; mark [confirmed] / [likely] / [uncertain]. Durable facts (addresses, variables, procedures) also go
   into the matching `docs/notes/` file (new topic: new note + a row in REFERENCE.md; superseded: `git mv` to
   `docs/archive/`). User-visible changes go in CHANGELOG.md.
3. Guard + `git status --ignored`, commit, push. PRs: one topic; say what changed, why and how it was tested (PC
   route and frames, emulator, hardware, `[perf]` numbers); end commit messages with your agent's co-author line.
