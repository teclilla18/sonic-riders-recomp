# Contributing

Thanks for helping. There are three ways in, and all of them matter:

1. **Play and report.** Bug reports from real hardware are as valuable as code: what happened, where,
   your clocks, and the log files (use the bug report form). Comparisons with real GameCube or Dolphin
   footage help with visual bugs.
2. **Write code yourself.** C (runtime, hardware models), Python (recompiler, tools), GLSL (renderer).
   See "How the project works" below.
3. **Contribute with a coding agent such as Claude Code.** You don't need to know the codebase; you
   need a Windows PC, your own disc image, and some tokens to spend. See the next section.

Issues labelled [`help wanted`](../../issues?q=is%3Aopen+label%3A%22help+wanted%22) are good places
to start; each one has the context an agent or a person needs.

## Contributing with Claude Code (or another coding agent)

1. Fork the repository on GitHub and clone your fork.
2. Build once so the agent can test: `SonicRidersSetup.bat` with your disc image, choosing **3**
   (Switch + PC). The PC build is how the agent checks its work.
3. Enable the game-data guard: `git config core.hooksPath tools/git-hooks`.
4. Start Claude Code in the repository folder. It reads [CLAUDE.md](CLAUDE.md) automatically: the hard
   rules (no game data, no personal details), how to build and test, and where the design notes are.
5. Give it one issue, for example:
   > Read CLAUDE.md and the parts of NOTES.md about this area, then work on issue #<number>. Reproduce it on
   > the PC build first (screenshots with SR_SCREENSHOT, scripted input with SR_INPUT), fix it, verify
   > the fix the same way, add what you found to NOTES.md, and commit.
6. **Review what it did before opening a pull request**: read the diff, run the PC build yourself, and
   check that it didn't add files from `build/` or `baserom/` (the guard should refuse them anyway).
   Say in the pull request that an agent wrote it and what you tested; if you could test on a Switch,
   say how it ran (with your clocks).

Agents work best on well-scoped problems with a visible result: a rendering difference with a
screenshot, a crash with a log, a performance hotspot with `[perf]`/`SR_PROFILE` numbers. Large
open-ended requests ("make it faster") tend to burn tokens without a result; ask for an investigation
written into NOTES.md first, then a fix.

## The one hard rule: no game data

Nothing derived from the game goes into git, issues or pull requests: no disc images, DOL/REL files,
extracted assets, disassembly, recompiled C (`build/gen/`), decomp-toolkit output (`symbols.txt`,
`splits.txt`), builds (`.nro`, `.exe`, `.nsp`), shader keys, memory cards, or console keys.
Logs from `sonicriders.log` are fine to paste.

Two checks enforce this, both running `tools/check_no_game_data.py`:

- the **pre-commit hook**. Enable it once after cloning:
  ```
  git config core.hooksPath tools/git-hooks
  ```
- **GitHub Actions** (`.github/workflows/guard.yml`) on every push and pull request, over the tree
  and the new commits.

The check refuses game-data folders and file types, files over 512 KB, binary files, the game's
files by hash, disassembly and recompiled-code lines, `key = <hex>` secrets, and personal home-folder
paths (write `<home>` or `C:\Users\<you>` in docs instead). If it refuses something you believe is
fine, say so in the pull request rather than working around it.

## Getting set up

Follow "Building from source" in the README: `SonicRidersSetup.bat` (or `setup\setup.ps1`) builds
everything from your own disc image. For day-to-day work the PC build (`-Target pc`) is the fastest
loop; the Switch build needs hardware or a Switch emulator to test.

## How the project works

[NOTES.md](NOTES.md) is the design log: architecture, decisions and why, debugging aids (`SR_*`
variables), findings, open items. It is written for whoever (person or coding agent) picks the work
up next. Read the relevant part before changing an area, and add what you learn: what was wrong, how
you found it, what the fix is, and what is still unproven.

Main places:

- `tools/recomp/`: the PowerPC → C recompiler; `config/GXEE8P/` its inputs (decomp-toolkit config,
  native replacements in `hle.txt`, code patches in `patches.txt`).
- `runtime/src/`: hardware models (`vi.c`, `di.c`, `gx_*.c`, `dsp.c`, `ax.c`, `exi.c`, `si.c`),
  scheduler, memory; `platform/switch/` and `platform/host/` for the two targets.
- `runtime/shaders/`: the GX shader shared by both renderers.

## Pull requests

- Describe what you changed, why, and **how you tested it** (PC, emulator, hardware; which stage or
  mode). Performance changes: before/after numbers from the log's `[perf]` lines.
- A clean build from a fresh clone must work (`setup\setup.ps1 -Iso <image> -Target both -NoSd -Yes`).
- Keep changes focused; one topic per pull request.
- Commit messages: a short imperative summary line, then the why if it isn't obvious.
- Contributions are under the project's license, MPL-2.0.
