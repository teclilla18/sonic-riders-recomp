# Architecture and design decisions

**Reference.** Moved from docs/REFERENCE.md on 2026-11-03 (content unchanged). Index: [REFERENCE.md](../REFERENCE.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## Project goal

**Goal (changed 2026-10-15):** play Sonic Riders natively on a modded Switch via **static recompilation**, as SwitchWakerHD does for Wind Waker HD:
- PowerPC code is translated to C ahead of time.
- The Dolphin SDK is reimplemented natively.
- Graphics are drawn with deko3d.

A matching decomp is **not** the goal. The Phase 2/3 decomp-toolkit matching plan below is superseded.

## Shared GLSL rules (uam)

The Switch compiles `runtime/shaders/` with uam, which miscompiles some patterns (found 2026-10-16 and 2026-10-29,
see HISTORY.md):
- Never index a vector or a local array with a runtime value. Use the explicit selects: `pick()` in
  `gx_common.glsl`, and `comp()`/`cinput()`/`ainput()`/`reg()` in `gx.frag`.
- In vertex shaders, avoid loop-carried vec4 accumulators seeded from a parameter, and `continue` in loops that
  accumulate vectors (`lights()` in gx.vert).
- After shader changes, every dksh in `build/switch/shaders/` must report `per_warp_scratch 0` (DkshProgramHeader at
  `programs_off`, offset 20).
- Specialized-shader keys must only include state the shader reads (gx_shader_key.c, gen_shaders.py): a stray bit
  makes every state miss and fall back to the ubershader (the 2026-10-26 slowdown). Check the `[deko3d]`
  "specialized shaders for N of M states" line in hardware logs.

## Device and replacement boundary as implemented

**Device and replacement boundary as implemented (revised from the plan below):**
- **Register-level device models**, which keep the game's own SDK code running: VI, DI (DVD), CP/PE/PI (GX FIFO), DSP/ARAM/AI, EXI (memory card + IPL SRAM/RTC), SI (controllers).
- **Renderer:** the guest side (`gx_render.c`, `gx_texture.c`) builds per-frame command lists. Backends: `platform/host/render_gl.c` (GL 3.3) and `platform/switch/render_deko.c` (deko3d). Shared ubershader GLSL in `runtime/shaders/`, embedded by `tools/embed_shaders.py` (uam for the Switch).
- **Memory card:** a new card is formatted natively exactly as the game's `__CARDFormatRegionAsync` does (serial = SRAM flash ID + CARD LCG from the format time). The IPL SRAM sets `flashIDCheckSum = ~sum(flashID) = 0xFF`, which `DoMount` checks for an already-unlocked card. The card reports "unlocked", so the DSP card-unlock task is never needed.
- **Native at function level**, only where the hardware semantics can't run natively:
  - `OSSaveContext`/`OSLoadContext`, i.e. the thread switch;
  - `OSLink`/`OSLinkFixed`;
  - no-ops: `__OSInitAudioSystem`, `__OSStopAudioSystem`, `__OSInitMemoryProtection` (real-mode BAT setup via `rfi`).

The whole OS scheduler, alarms, mutexes and message queues run recompiled, which proved simpler and more faithful than replacing the APIs. All replacements are listed in `config/GXEE8P/hle.txt`.

## Plan: ahead-of-time specialized shader cache

Implemented since (tools/gen_shaders.py, gx_shader_key.c); the original plan:

**Plan: ahead-of-time specialized shader cache (the GPU fix; user's preferred "built in advance" approach)**
- Why: the fragment ubershader costs ~33 ms for ~120 draws at 2x. deko3d has no runtime GLSL compiler, so specialized shaders must be compiled with uam at build time.
- **Key:** the fragment state that drives control flow:
  - TEV stage count; per stage: order (map, coord, enable, channel), color/alpha env, ksel, swap selects, IND_CMD;
  - the indirect stage count and IREF; alpha test compare/logic; the EFB alpha-update rules.
  Register *values* (TEV colors, konst, matrices, alpha refs) stay uniforms, so one shader covers many materials.
- **Collect:** the PC build logs every new key (`SR_SHADER_KEYS=<file>`). Play through the game, or merge key files from the user's Switch (`sdmc:/switch/sonicriders/shader_keys.txt`, written when a key misses the cache).
- **Build:** `tools/gen_shaders.py` emits straight-line GLSL per key (the gx.frag logic with every select resolved at generation time), compiles each with uam, and embeds a sorted key → dksh table. The GL backend can use the same generated GLSL to check equivalence against the ubershader on PC.
- **Runtime:** look up the key per state; on a miss use the ubershader and record the key for the next build.

## Architecture decisions (2026-10-15)

| Decision | Choice | Why |
|---|---|---|
| Recompiler | Fork ZeldaWWHDRecomp's translator (`ppc2c.py`, `crlive.py`, `leaflocal.py`, `ppc.h`, fres/frsqrte tables; MPL-2.0, upstream commit f10b566) + new GameCube front end `tools/recomp/gcrecomp.py` | Espresso and Gekko share the 750 user ISA, incl. paired singles, GQR quantization and `dcbz_l`. Its inline C with register-in-locals passes is proven at 30 fps on Switch hardware. **DolRecomp** (github.com/ExpansionPak/DolRecomp, GPL-3.0) was evaluated: it supports DOL+REL natively but is emulator-style (full MSR/exception model, callback memory access, a helper call per op), which is slower on ARM. Kept as a reference only. |
| Function discovery | decomp-toolkit v1.8.4 analysis (`config/GXEE8P/config.yml`), with `detect_strings: false` (a dtk bug in REL sec 2) | Understands MWCC idioms and REL relocations. Not used for matching. |
| Jump tables | Structural: a data object whose words all point mid-function into one function. The recompiler reports any `lwzx/mtctr/bctr` without a table. | dtk names only some tables `jumptable_*` (others like `@740`). |
| REL placement | Linked statically at `REL_BASE = 0x81800000`, just above the 24 MiB of MEM1 (the runtime backs 28 MiB), by the recompiler and identically at runtime (`loader.c`). The game still loads and "links" its own copy from its heap, and the native OSLink points that copy at the fixed image. | Fixed addresses for recompiled code, and the game keeps exactly its normal memory. (First tried 0x81500000 with ArenaHi lowered, which would have cost the game ~2.5 MB.) |
| Guest memory | Window indexed by physical address, `ea & 0x3FFFFFFF` (folds the 0x8/0xC mirrors; locked cache → `0x20000000`). Loads/stores in `0xC8000000–0xCFFFFFFF` go to `mmio.c`. | One AND + one compare per access, and every hardware access, incl. inline GX FIFO writes in game code, is caught. |
| HLE boundary | **Native:** OS, DVD, VI, PAD, SI/EXI (stubs), CARD, DB/TRK (stubs), AI, AR/ARQ, DSP (mailbox + AX ucode HLE). **Recompiled:** GX (+ native FIFO/CP/PE model), AX, MTX, MSL, CRI middleware, game. | OS must be native: context switches, rfi, interrupts. GX on GameCube is a stream of FIFO register writes, partly inlined into game code, plus display lists. A FIFO parser is needed anyway, so the FIFO is the natural boundary rather than ~200 GX functions. AX via its DSP protocol reuses the real AX library's exact semantics. |
| Renderer | GX FIFO → state → shared GLSL (TEV ubershader first); backends: desktop OpenGL (dev/testing), deko3d (Switch, shaders via uam) | Lets me test without hardware; the user's requirement of deko3d on Switch holds. |
| Host toolchain | clang 23 (winget LLVM) + MSVC 2022 libs; Switch: devkitPro devkitA64 GCC 16, libnx, deko3d, uam (already installed) | `musttail` and the GNU builtins are required by the generated code. |
| Disc access | The runtime reads the user's ISO directly (FST lookup in `disc.c`) | The player only copies their own ISO; nothing is extracted or shipped. |
| License | Project code MPL-2.0 (compatible with the forked files) — **[tentative]**; third_party/LICENSE-MPL-2.0 | Forked files must stay MPL-2.0. |

### SDK map of main.dol (from dtk names + Dolphin `totaldb.dsy` signatures via `tools/sigmatch.py`)
Matching: 804 confident matches (≥0x24 bytes, exact size). Smaller matches are unreliable (e.g. a lone `blr` "matches" game methods from other titles).

| Range | Library |
|---|---|
| 80003100–800055E0 | `.init` (`__start`, `__init_*`, memset/memcpy) |
| ~80005590–80009800 | game bootstrap, `main` @ 80007A48 |
| 800097CC–80011FF4 | OS (+ DB) |
| 80011FF4–80016624 | DVD |
| 80016624–80018700 | VI |
| 80018728–80019ECC | PAD |
| 8001A7F4–8001C5A0 | AR / ARQ |
| 8001C5A0–80025E5C | unnamed, ~39 KB (AX/AXFX/MIX likely; no signatures) **[uncertain]** |
| 80025E6C–800269E8 | DSP |
| 800269E8–8002D38C | CARD |
| 8002DA78–80034F2C | GX |
| 80034F4C–80042BC8 | MSL / runtime |
| 80042BC8–80046330 | EXI, SI |
| 80046330–8004C4D4 | MetroTRK |
| 8004C4F8–8004DA00 | MTX, DB |
| 8004DA00–800E2F20 | ~600 KB unnamed: CRI ADX/Sofdec/SNDF + other middleware **[uncertain]** |

`main` (80007A48):
1. Calls OSInit, `fn_8000629C`, `fn_80063204`, `fn_80006CD0`, `fn_800E2918`.
2. Loads `SonicR.str` and `_Main.rel` with `fn_80007CE8(name, 1)`.
3. Calls `fn_8000DCBC` (likely OSSetStringTable) and `fn_8000E288` = OSLink.
4. Jumps to `module->prolog`.

## Naming decisions
(none yet)
