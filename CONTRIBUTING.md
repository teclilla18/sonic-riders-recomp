# Contributing

Thanks for helping. Bug reports from real hardware are as valuable as code.

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
