# Reference projects (other recompilations we borrow ideas from)

Kind: Reference. Ideas only: check each project's license before reusing any code (ours is MPL-2.0). What was
adopted, with dates: grep the project name in `docs/HISTORY.md`, `docs/archive/` and `docs/notes/plans-2026-11.md`.

| Project | What it is | What we took or look at |
|---|---|---|
| ZeldaWWHDRecomp | Wind Waker HD (Wii U) static recompilation | Our PowerPC-to-C translator is forked from it (MPL-2.0: `tools/recomp/ppc2c.py`, `crlive.py`, `leaflocal.py`, `ppc.h`); `docs/performance.md` ideas (keep game speed, flip when ready) |
| centollOS/SwitchWakerHD | Its Switch port | Settings menu layout, hold-Minus menu, one-time caches, performance rounds and PGO notes, the desktop installer |
| ExpansionPak/DolRecomp (also ModernGekko) | GameCube DOL/REL recompilers | Evaluated 2026-10-15: emulator-style, slower on ARM; reference only (GPL-3.0) |
| NotRexed/Hookshot | Ocarina of Time recompiled to Windows/Linux, Vulkan | Ideas only (GPL-3.0); list in notes/plans-2026-11.md "Ideas from Hookshot" |
| Artknighty/tagforcerecomp-preview | PSP static recompilation to Windows | Mod ideas (hooks, HD textures); notes/plans-2026-11.md "Ideas from Tag Force Recomp" |
| zofo007/NFS-Carbon-NX | A Switch recompilation port | Idea source, nothing adopted yet |
| dusklight | Switch port that drives the console's clocks | Looked at 2026-10-25; skipped (it overclocks, not wanted) |
| UnleashedRecomp-NX | Switch port of UnleashedRecomp | Looked at 2026-10-25; nothing new for us |
| Dolphin | GameCube emulator | Picture and behaviour reference (FIFO logs, texture dumps); AX microcode format |
