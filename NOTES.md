# Sonic Riders (GameCube) → native Switch port — Notes

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## ▶ RESUME HERE (read first in a new session)

**Goal (changed 2026-10-15):** play Sonic Riders natively on a modded Switch via **static recompilation**, as SwitchWakerHD does for Wind Waker HD:
- PowerPC code is translated to C ahead of time.
- The Dolphin SDK is reimplemented natively.
- Graphics are drawn with deko3d.

A matching decomp is **not** the goal. The Phase 2/3 decomp-toolkit matching plan below is superseded.

**Status after session 5 (2026-10-16):**
- **Windows host build:** the intro, the opening FMV, the title screen, the save-file menu and **creating a save** (verified with scripted input), with controller input, at 60 fps.
- **Switch build** (verified in Eden v0.1.1): logos, FMV, title screen, the attract-mode demo race in 3D, the settings menu and widescreen.
- **Hardware tests so far:**
  1. Black screen, then `image pool exhausted`. Fixed in session 3.
  2. Black screen. Fixed in session 4 (the uam miscompile below).
  3. Picture OK. The user reported:
     - the title screen is choppy and in slow motion;
     - black side bars (wants 16:9 and a hold-Minus menu, like SwitchWakerHD);
     - no sound;
     - creating a save fails with "not present or corrupted".
     Everything but sound was addressed in session 5. Test 4 is pending.
- **Audio is silent everywhere**: the AX stand-in consumes command lists but does not mix yet. **This is the next big task.**

**Session 5 changes (from hardware test 3):**
- **Save creation:** `exi.c` rewrote the whole 512 KB card file after every page program or erase (~1250 per save, about 600 MB of SD writes). On the Switch that outlasted the CARD library's timeout alarms. The card is now written by a background thread once writes have been idle for 0.5 s, and at exit. Verified on PC: a fresh card gets `SONICRIDERS_GAMEDATA_01`.
- **Texture cache thrash (the main slowdown):** the cache's slot index `(key ^ key >> 21) % 8192` ignored most address bits, so same-size textures evicted each other every frame: hundreds of re-decodes and 1–6 MB/frame of uploads, with "0 same key" in the churn log. It is now a two-way set with a splitmix index. Hashing is also cheaper: a 512-sample hash every frame, plus the full hash every 16th frame (staggered).
- **GX state:** a state change used to copy 8 KB of registers (about 33 MB/frame in races), and overflowed the deko3d UBO area, silently dropping draws. Now:
  - the state uniform block is 1.7 KB: BP, XF registers, konst, and a table of XF-memory chunks;
  - XF memory travels as 64-word chunks, copied only when dirty (`g_gx.xfm_dirty`, set in `gx_fifo.c`), in a per-frame pool. It is read through `XFM_WORD()`: an SSBO on deko3d, a buffer texture on GL;
  - identical consecutive states are dropped.
  The vertex shader reads XF registers and memory; the fragment shader reads BP and konst only.
- **DVD completion is asynchronous:** data is copied at command start, and completion (TSTART clear plus the interrupt) comes **2 ms + len/32 MB/s** later, from a timer thread in `di.c`.
  - Why: the SDK's `DVDLowRead` (`fn_80012408`) writes DICR and *then* arms its timeout alarm (`OSSetAlarm(AlarmHandlerForTimeout)`).
  - A completion before that (instant, or 200 µs plus a host stall) let the alarm fire later as a drive timeout, and loading stopped at "Now Loading" (≈1 in 20 runs).
  - A `patches.txt` diagnostic logs `[dvd] the SDK's drive timeout alarm fired` if that ever happens again.
- **Widescreen:** recompiler code patches (`config/GXEE8P/patches.txt`, documented in `gcrecomp.py`) are the native form of gamemasterplc's 16:9 Gecko code. Both projection sites multiply the aspect by `g_aspect_scale` (`settings.c`): `fn_800645DC` at entry, and REL text `0x14115C` after the `fmadds`. HUD/2D elements are stretched, as with that code. Muzzarino's code moves the HUD but is known to break on Egg Factory and Sand Ruins.
- **Settings menu** (`overlay.c`, `settings.c`):
  - open: hold Minus 0.5 s (Switch), F1 or the controller's Back (PC);
  - options: Screen (4:3 / 16:9 widescreen / 16:9 stretched) and an FPS counter; saved to `settings.ini` next to the saves;
  - drawing: software-rendered with `runtime/third_party/stb_easy_font.h` (public domain / MIT), then composited by both backends with `runtime/shaders/overlay.*`;
  - `SR_TEST_MENU=1` opens it at start (screenshots).
- **Diagnostics:**
  - `env.txt` on the Switch SD folder (`KEY=VALUE` lines) sets debug variables such as `SR_WATCHDOG`;
  - `SR_WATCHDOG_NOTRACE=1` keeps the dump without the function trace, which hides timing races;
  - the dump shows render-thread handoff state, DI registers, pending OS interrupts and thread suspend counts.
- **Hang hunting:** run Eden 20–30 times (`hangloop.sh`) with an `env.txt` watchdog dump.
  - The "Now Loading" hang is explained by the DVD timeout race above.
  - One other early freeze was seen once: interrupts pending with a delivery queued (`posted 1`) while no guest code ran. Watch for it; suspects are the native thread handoff in `sched.c`, or a host wait while a guest context is current.
- **PC (GL) only: the race HUD's digits and labels are garbled** (rainbow noise). The Switch build draws them correctly, so it's a GL backend difference (sampler or texture setup) to investigate; the PC build is a development host only.
- **Eden after these fixes:** the demo race runs at 50–59 fps (was 23–38), and texture uploads are 40–70 KB/frame (were 3.6–4.5 MB).

**Test scripts (scratchpad, not committed; recreate if missing):** `swtest.sh` (build the Switch NRO, stop on build errors, deploy to Eden, run, filter the log), `eden_run.ps1` (start and kill eden-cli), `bmp2png.py`, `bmpstat.py`.

**Session 4: why the Switch screen was black (fixed)**
- **uam (deko3d's shader compiler) miscompiles a runtime component index into a uniform-buffer vector.** `xfm[i >> 2][i & 3]` in the vertex stage returned wrong values for components 1–3. Every vertex was therefore transformed by a garbage matrix, and nothing rasterized.
- The fragment shader's runtime-indexed local arrays (`r[4]`, `cin[16]`, `ain[8]`) also needed **194 KB per-warp scratch memory**, far beyond the queue default.
- **Rule for the shared GLSL:** never index a vector or a local array with a runtime value. Use explicit selects instead: `pick()` in `gx_common.glsl`, and `comp()`/`cinput()`/`ainput()`/`reg()` in `gx.frag`.
- Check after shader changes: both dksh files in `build/switch/shaders/` must report `per_warp_scratch 0`. Read it from the DkshProgramHeader at `programs_off`, offset 20.
- How it was found: a self-test (EFB clear → blit → capture) plus re-rendering one frame with state overrides and tiny debug shaders, bisected inside Eden. That scaffolding was removed after the fix; the method is recorded here.

**Testing without hardware: Eden v0.1.1** (`build/ext/eden-v0.1.1/app`, gitignored; portable `user/` folder)
- **Use v0.1.1.** v0.2.1 aborts in `dkDeviceCreate`, even with devkitPro's stock `deko_basic` sample.
- Keys: Eden needs the user's own console keys in its `user/keys/` folder. Never commit them; `*.keys` is gitignored and blocked by the pre-commit hook.
- SD card: `user/sdmc/switch/sonicriders/` holds the NRO, an ISO copy, saves and the log.
- Run: `eden-cli.exe "user\sdmc\switch\sonicriders\sonicriders.nro"`. Don't start it minimized: rendering stalls. The user is fine with the window being visible.
- Eden has no process handle for homebrew, so guest memory uses the `svcMapMemory` fallback there.
- The port's own `shot_<frame>.bmp` self-captures are how to look at frames. Don't screen-capture the desktop: other windows end up in the image.

**Performance (Eden numbers, not hardware; the `[perf]` line has the breakdown):**
- **FMV ~42 fps on console and in Eden:** the guest is CPU-bound (guest idle 0 ms). The recompiled Sofdec MPEG decoder is too slow on ARM. Late frames show half-decoded images. Textures, render wait, disc reads and timer lateness are all negligible.
- **Menus:** 60 fps, but texture lookup costs ~4 ms/frame (the hash of every bound texture every frame).
- **Demo race:** 28–38 fps, ~4000 draws/frame, 3–4 MB/frame texture re-uploads, texture lookup 5–16 ms/frame, render thread 21–26 ms/frame.

**Next steps, in order:**
1. **Hardware test 4** (user): the slow motion should be gone or much better; check that saves work and the hold-Minus menu and widescreen. Send `sonicriders.log` and `shot_*.bmp`.
2. **Audio (AX HLE)** in `ax.c`, from the AX command-list and parameter-block formats:
   - command list: PB list, ADPCM/PCM8/PCM16 decode from ARAM, sample-rate conversion, volume and ramps, aux buses, output to the AI DMA buffer;
   - then host output: SDL audio (PC), audout/audren (Switch).
3. **The rare loading hang** (see above).
4. **Switch performance:**
   - overlap CPU and GPU (`dkQueueWaitIdle` per frame);
   - EFB scale option (1x or 1.5x for handheld);
   - batch draws;
   - profile the FMV decoder (42 fps).
5. **Renderer accuracy:**
   - fog (the blue tint);
   - the file-select background is black;
   - indirect textures, Z textures, mipmaps, EFB copy formats, EFB→RAM write-back.
6. Widescreen HUD placement (Muzzarino's approach), only if wanted.

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

**Device and replacement boundary as implemented (revised from the plan below):**
- **Register-level device models**, which keep the game's own SDK code running: VI, DI (DVD), CP/PE/PI (GX FIFO), DSP/ARAM/AI, EXI (memory card + IPL SRAM/RTC), SI (controllers).
- **Renderer:** the guest side (`gx_render.c`, `gx_texture.c`) builds per-frame command lists. Backends: `platform/host/render_gl.c` (GL 3.3) and `platform/switch/render_deko.c` (deko3d). Shared ubershader GLSL in `runtime/shaders/`, embedded by `tools/embed_shaders.py` (uam for the Switch).
- **Memory card:** a new card is formatted natively exactly as the game's `__CARDFormatRegionAsync` does (serial = SRAM flash ID + CARD LCG from the format time). The IPL SRAM sets `flashIDCheckSum = ~sum(flashID) = 0xFF`, which `DoMount` checks for an already-unlocked card. The card reports "unlocked", so the DSP card-unlock task is never needed.
- **Native at function level**, only where the hardware semantics can't run natively:
  - `OSSaveContext`/`OSLoadContext`, i.e. the thread switch;
  - `OSLink`/`OSLinkFixed`;
  - no-ops: `__OSInitAudioSystem`, `__OSStopAudioSystem`, `__OSInitMemoryProtection` (real-mode BAT setup via `rfi`).

The whole OS scheduler, alarms, mutexes and message queues run recompiled, which proved simpler and more faithful than replacing the APIs. All replacements are listed in `config/GXEE8P/hle.txt`.

**Debugging aids:**
- `SR_WATCHDOG=<seconds>`: records function entries and, when time runs out, dumps them, the last device accesses and every guest thread with its state, priority, wait queue and guest backtrace.
- `SR_SCREENSHOT=<frame>`: saves `build/shot_<frame>.png` (host).
- `SR_GXDEBUG=<frame>`: logs that frame's viewport/scissor/copies/draws.
- `SR_HEADLESS=1`: no window.
- `SR_INPUT="1500:START,1510:-START,..."`: scripted button presses at VI field counts.
- `SR_CARD=<path>`: memory card image.
- `SR_TEST_MENU=1`: start with the settings menu open.
- Switch: `env.txt` in the SD folder sets these variables; `SR_WATCHDOG_NOTRACE=1` skips the function trace.
- `tools/showfn.py ADDR|NAME`: print a function's disassembly from dtk's output.
- `build/run.ps1 <seconds>` (local helper, not committed): rebuild + run + summarize.

**How to build/run (host, Windows):**
```
build/tools/dtk.exe dol split config/GXEE8P/config.yml build/GXEE8P   # once (analysis; writes config/GXEE8P/*symbols.txt)
python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen        # recompile (also run by ninja)
. tools/host/env.ps1                                                   # PowerShell: clang + MSVC/SDK paths (vcvars64.bat fails on this PATH)
python configure.py ; ninja                                            # ninja: %APPDATA%\Python\Python312\Scripts\ninja.exe
build/host/sonicriders.exe "baserom/Sonic Riders (USA) (En,Ja,Fr,De,Es,It).iso"
```
After a fresh clone, run `git config core.hooksPath tools/git-hooks` (the game-data guard).

**GitHub:** https://github.com/teclilla18/sonic-riders-recomp (**private**). gh CLI: `gh`, logged in as teclilla18.
- **Push at the end of every work session.**
- Before pushing, check `git status --ignored`: no ISO, DOL, REL, assets, disassembly, recompiled output or dtk symbol files.
- If anything questionable would be included, stop and ask the user.

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

## Phase 1 — Recon (2026-10-15)

### Backup inventory
The backup dir contained exactly one file. The original was moved to `<backups>\` on 2026-10-15 at the user's request (same-drive rename; SHA-1 re-verified afterwards; read-only attribute set). Working copy: `baserom/` (gitignored).

| File | Size (bytes) | Format |
|---|---|---|
| `Sonic Riders (USA) (En,Ja,Fr,De,Es,It).iso` | 1,459,978,240 | GameCube GCM (full-size raw disc image) |

### Hashes
| File | SHA-1 | MD5 |
|---|---|---|
| Disc image (`.iso`) | `ce0cecf57034bef8bab7d6f588ce92c85a849613` | `80155de587c0233e39ab545c2e771749` |
| `main.dol` (extracted, 1,195,200 B) | `e90a13504974b917a675d59f07014e0e817c5e5d` | `46874b58c6f1372b3bfce69ca6d8c6d6` |
| `_Main.rel` (extracted, 2,572,344 B) | `1607fbb1ccabf116642c3c59ee9b21c304a1db26` | `35a2a787ed42f1f1ba82570bb3a67905` |
| `SonicR.str` (extracted, 40 B) | `caf827eb8c510744c0825304f68ae89250bb11a3` | `e4fb96ae3bf1404ebbdacab1d059e79d` |

The copy in `baserom/` has the same SHA-1 as the original. [confirmed]
**Redump match** [confirmed 2026-10-15]: redump.org/disc/7133/ "Sonic Riders", serial DL-DOL-GXEE-USA, edition "Original, Player's Choice". MD5 `80155de587c0233e39ab545c2e771749` and SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613` match ours exactly. Size 1,459,978,240, CRC-32 `1dabe6ee`. So the dump is clean and verified, and is the same disc as both the original and the Player's Choice pressings.
Other Redump entries, for reference only: PAL DL-DOL-GXEP-EUR (/disc/19825/), JPN DL-DOL-GXEJ-JPN (/disc/9328/).

### Disc header [confirmed]
- Game ID `GXEE8P` (G=GameCube, XE=Sonic Riders, E=USA, 8P=Sega), disc 0, revision 0, region code 1 (NTSC-U)
- Internal title `SonicRiders`
- DOL offset `0x20300`, FST offset `0x144000` (size `0x72b1`)
- Apploader date `2004/11/10`, entry `0x81200258`
- FST: 1,669 files, all at the disc root. By size: SFD (Sofdec video) 515 MB, extensionless game data 500 MB (1,563 files), ADX audio 294 MB, AFS archives 79 MB, `.dat` 9 MB, one `.rel`, one `.str`, one `.bin`, `opening.bnr`. Full listing: `baserom/fst_listing.txt`.

### Executables
The code is split across two executables:

**1. `main.dol`**: DOL format, PowerPC 750CL "Gekko" (32-bit, **big-endian**). [confirmed]

| Section | DOL file offset | Load address | Size | Probable name |
|---|---|---|---|---|
| text0 | 0x000100 | 0x80003100–0x800055E0 | 0x24E0 | `.init` |
| text1 | 0x0025E0 | 0x800055E0–0x800E2F20 | 0xDD940 | `.text` |
| data0 | 0x0DFF20 | 0x800E2F20–0x800E2F40 | 0x20 | `.ctors` (all zero) |
| data1 | 0x0DFF40 | 0x800E2F40–0x800E2F60 | 0x20 | `.dtors` (all zero) |
| data2 | 0x0DFF60 | 0x800E2F60–0x800ED2E0 | 0xA380 | `.rodata` |
| data3 | 0x0EA2E0 | 0x800ED300–0x80125E00 | 0x38B00 | `.data` |
| data4 | 0x122DE0 | 0x8021B8C0–0x8021BB40 | 0x280 | `.sdata` |
| data5 | 0x123060 | 0x8021C340–0x8021CFA0 | 0xC60 | `.sdata2` |
| bss | — | 0x80125E00–0x8021CFA4 | 0xF71A4 | `.bss` (+`.sbss`/`.sbss2`, interleaved) |

Entry point: `0x80003154`. Its code is the standard Metrowerks `__start` (`bl __init_registers; bl __init_hardware; ...`). [confirmed by byte pattern]
Section names come from the standard MWCC layout. [likely]

**2. `_Main.rel`**: REL module (relocatable, loaded at runtime by the DOL). Holds most of the game code. [confirmed]
- Module id 1, REL version 3, 14 section slots, align 0x20, bss-align 0x40, fixSize 0x1FF360
- Prolog: section 1 + 0x1BC310. Epilog and unresolved handlers: none (0)
- Imports: relocations against itself (module 1) and against the DOL (module 0)

| Section | File offset | Exec | Size | Probable name |
|---|---|---|---|---|
| 1 | 0x0000BC | yes | 0x1C10DC (1.8 MB) | `.text` |
| 2 | 0x1C1198 | no | 0x16914 | `.rodata` [uncertain] |
| 3 | 0x1D7AC0 | no | 0x27890 | `.data` [uncertain] |
| 4 | — | no | 0xBA6F4 | `.bss` |

- `SonicR.str` contains `D:\Projects\SonicR\elf\master\_Main.plf`. This is the makerel string table: the REL's original link output was `_Main.plf`, in project folder `SonicR`. [confirmed]

### Toolchain fingerprints
- **Compiler: Metrowerks CodeWarrior (MWCC) for GameCube.** Evidence: the `Metrowerks Target Resident Kernel for PowerPC` string, `Metrowerks CW runtime library initializing default heap` (from `GCN_Mem_Alloc.c`), the MW `__start`, and the section layout. [confirmed it's MWCC; **exact version unknown**]. Given the late-2004 build dates, it is probably one of the GC 2.x releases (2.6/2.7 era), but **this is a guess** and can only be settled by test-compiling functions.
- **Language: probably C, not C++.** `.ctors`/`.dtors` are empty, with no `__init_cpp_exceptions` entry. There are no mangled names, no RTTI, and no `extab`/`extabindex` sections. [likely]
- **Nintendo Dolphin SDK, revision 0x2301:**
  - OS and GX: release build Nov 10 2004
  - VI: Apr 7 2004
  - DVD, PAD, AI, AR, ARQ, AX, DSP, CARD, EXI, SI: Apr 5 2004
  - These are release (non-debug) libraries. OS and GX were patched later, which is consistent with a late-2004 SDK patch. [confirmed strings]
- **CRI Middleware** (audio/video), all in the DOL:
  - ADXT/GC 9.31, ADXGC 1.21, ADXGCSDK "20Apr2004Patch1", ADXF 7.18, CVFS 2.37, LSC 2.18, SJ 6.28, SVM 1.54, MFCI 1.09, GCCI 1.09, AXRNA 1.04, SKG 0.64 (built Oct 8 2004)
  - Sofdec: MWSFD 3.33, SFD 1.947, SFH 1.19, MPV 1.933, MPS 1.924, DCT 1.945, SFX 2.08, SUD 0.05, CFT 1.57 (built Sep 22 2004)
  - **SNDF Ver.2.21a, built Dec 5 2005**. This is the newest timestamp in the binary. [confirmed]
- Debug/RTOS remnants: TRK/ODB exception hooks in OS (`ODBExceptionDestination`). These are normal for SDK builds.

### Symbols / map files
- **No map files, no ELF, no debug symbols on the disc.** [confirmed: only one `.rel`, no `.map`/`.elf`/`.plf`]
- Only 3 source filenames leaked: `OSThread.c`, `dvdfs.c` (SDK), and `GCN_Mem_Alloc.c` (MW runtime). There are no game-side source filenames.
- The game code has very few strings: about 900 in the REL, mostly asset names (`*.ADX`, `*.SFD`) and save-file names (`SONICRIDERS_GAMEDATA_%02d`, `SONICRIDERS_GHOSTDATA_%02d`).
- Practical consequence: SDK/MSL/CRI functions can be identified by signature matching against known SDK libraries. **Game functions must be named by hand.**

### Leads
- **GameCube NPDP prototype**: the Sonic Retro article does **not** mention symbols, a map file, an ELF, or debug info. [confirmed, but absence of a mention is not proof of absence]
  - Live wiki is behind an Anubis bot-check, so I read the Wayback snapshot of 2025-01-18. The 2026-03-05 snapshot captured only the bot-check page.
  - The article's full substance: found on an NPDP cartridge by MSX, dumped March 2021, "appears to be near-final, if not final".
  - Hidden Palace has a matching entry (hiddenpalace.org/Sonic_Riders_(NPDP_prototype)), not inspected. The Sonic Retro discussion thread was not readable (bot-check).
  - Nothing was downloaded.
- **PS2 prototype 0.931 (7 Oct 2005)**: a community restoration (Sewer56 index → Sonic Retro thread 39828) lists "Partial Symbols (Function, Variable Names)" from a debug executable. [confirmed that the claim exists; not inspected]
  - It is a **different platform** (MIPS, different compiler), so it is useless for byte-matching.
  - It *might* help name functions and structs by cross-referencing logic. [uncertain]
- **Sewer56.SonicRiders / Riders.Tweakbox** (github.com/Sewer56): reverse-engineered PC-version structs and data, with GameCube file-format tools. Possible naming reference. [uncertain]
- **Sonic Adventure DX decomp**: verified on github.com directly (via the GitHub API): https://github.com/doldecomp/sadx. Description "Sonic Adventure DX", not archived, last push 2024-08-01, 48 stars, no license file. [confirmed]
  - It is a dtk-template project.
  - Linker GC/2.0; libraries pinned to GC/1.2.5n, 1.3, and 1.3.2. A 2003 game from a different team, so **not evidence** for Riders' compiler. Use it as a structural reference only.
  - (The earlier link I gave pointed at a mirror, gitblind.noratr.app. Disregard it.)
- A Windows PC version of Sonic Riders (2006) exists. Its binary could help with naming and logic cross-reference, though not with matching. [uncertain]
- No existing Sonic Riders decomp found in a quick search (2026-10-15).

### Scale estimate
- Code to decompile: DOL `.text` 0.9 MB (much of it SDK + CRI + MSL) plus REL `.text` 1.8 MB (game code). That is roughly 680k PowerPC instructions in total.
- The SDK portions can likely reuse existing community decomps of Dolphin SDK 2004-era libraries. The CRI middleware has no public source.

### Re: end goal (Switch)
A *matching* decomp yields C source that rebuilds the GameCube binary. Running it natively on Switch is a **separate port project** on top of that:
- Replace GX/OS/DVD/PAD/AX/CRI calls with a portability layer (e.g. SDL + OpenGL/Vulkan)
- Handle endianness of all asset data
- Handle 32-bit pointer assumptions
- Replace the closed-source CRI ADX/Sofdec decoders

This should be planned for, but it does not change Phases 2–4.

### Files in `baserom/` (gitignored, never commit)
- the ISO copy
- `main.dol`, `_Main.rel`, `SonicR.str`
- `fst_listing.txt`, `recon_strings_*.txt`

## Phase 2 — Tooling plan for a matching decomp (2026-10-15) — SUPERSEDED by the recompilation strategy above; kept for reference

### Recommendation: decomp-toolkit + dtk-template + objdiff
- **decomp-toolkit (dtk)**: https://github.com/encounter/decomp-toolkit, latest v1.8.4 (2026-09-09). The de-facto standard for GC/Wii decomps.
  - Analyzes DOL **and REL**, finds function boundaries, and detects MW runtime and SDK signatures, strings and objects.
  - Splits code into relocatable `.o` objects from `symbols.txt` + `splits.txt`.
  - Rebuilds DOL and REL (`elf2dol`, `rel make`), and reads ISOs directly via its VFS.
- **dtk-template**: https://github.com/encounter/dtk-template, CC0 license.
  - `configure.py` generates a ninja build. It auto-downloads pinned versions of dtk, objdiff-cli, PPC binutils, sjiswrap and the MW compiler pack into `build/` (project-local, not system-wide).
  - Per-library `mw_version` and cflags, REL support built in, `build.sha1` verification, progress reports.
  - The template currently pins dtk v1.8.3, objdiff v3.6.1, compilers 20251118, binutils 2.42-2. These can be bumped.
- **objdiff**: https://github.com/encounter/objdiff, latest v3.8.2 (2026-09-29). Per-function diff of our compiled `.o` against the target `.o` that dtk split out.
  - Understands PPC relocations, so address differences don't show as false mismatches.
  - The CLI is used by the build for progress; the optional GUI gives a live diff that updates on save.
- **Rejected**:
  - splat: N64/PS1/PS2-oriented, no DOL/REL support.
  - asm-differ: redundant with objdiff on GC.
- **Optional, later**:
  - m2c: produces PPC → C first drafts.
  - Ghidra + a GameCube DOL/REL loader: for reading and naming.
  - decomp.me: **note** that scratches there are public, so uploading asm publishes copyrighted code.

### How the build/verify works (differs from "reassemble from .s")
dtk does not reassemble text `.s` files. Instead:
1. It splits the original binaries into relocatable objects at the split points (`build/GXEE8P/obj/...`).
2. The build links those objects plus any matching compiled C with the original Metrowerks linker (`mwldeppc`).
3. It runs `dtk elf2dol` for the DOL and `dtk rel make` for the REL, then checks the SHA-1 of the outputs against `config/GXEE8P/build.sha1`.

As decompiled C files reach 100% match, they replace the extracted objects. Disassembly (`.s`) is still written to `build/GXEE8P/asm/` for reading.

Verification targets `main.dol` + `_Main.rel` hashes (recorded above), **not the ISO**. Rebuilding the ISO itself is out of scope and unnecessary.

### Compiler-version test plan
Expect **several compilers** in one binary:
- Nintendo's prebuilt SDK libraries: typically GC/1.2.5n for SDK libraries of this era. [likely, to be verified]
- CRI's prebuilt libraries: their own compiler.
- Game code + MSL/runtime: whatever CodeWarrior Sega used.

Each library gets its own `mw_version`. Steps:
1. **Structural fingerprints, no compiling.**
   - Linker: `.ctors`/`.dtors` layout and padding (the template documents a GC 1.0–2.6 vs 2.7+ difference), section alignment gaps, and the odd absence of `__init_cpp_exceptions` in `.ctors`.
   - MSL/Runtime: once dtk labels the runtime functions (`__save_gpr`, `memcpy`, `__div2u`, printf internals…), compare them against existing matched MSL/Runtime builds for each CW version. The runtime ships with the CW install, so a byte-match pins the toolchain release.
2. **Empirical sweep.**
   - Pick 6–10 REL game functions that exercise version-sensitive codegen:
     - int↔float conversion constants
     - switch jump tables
     - loops (unrolling/strength reduction)
     - struct copies
     - small inlined helpers
     - register-pressure prologues (`stmw` vs `_savegpr`)
   - Write each in C once. Compile with every candidate in the compiler pack (GC/1.3.2, 2.0, 2.5, 2.6, 2.7, 3.0a3, 3.0a5.2…) crossed with plausible flag sets (`-O4,p` / `-O4,s` / `-O3`, `-inline auto|deferred`, `-str reuse|pool`, `-sdata`/`-sdata2` thresholds).
   - Score each combination with `objdiff-cli diff` (match %).
   - The version/flag set that gives 100% across *all* samples wins. If several do, take the range and keep sampling.
   - I'll script this sweep in `tools/` and record the results matrix here.
3. The **linker** version is chosen separately: the one whose linked output reproduces the section layout/padding byte-for-byte.

## Open questions
0. (recomp) Which unnamed DOL functions are OSLink, OSSetStringTable, the AX library, and the file loader `fn_80007CE8`? Needed for hle.txt.
0b. (recomp) Does any code jump/call through a computed address that is not a dtk function entry? The runtime aborts with "dispatch to X: not a function entry"; X so far: only the null callback in OS thread code (expected, OS will be native).
0c. (Switch) Memory budget: guest MEM1 24 MiB + ARAM 16 MiB + generated code (~tens of MB) is far below SwitchWakerHD's; applet mode may even suffice. **[uncertain until hardware test]**
1. (matching decomp only, no longer needed) Exact MWCC version(s) and flags.
2. Is the game code really pure C? Check for C++ patterns in the REL (vtable-like tables, `__ptmf`, etc.).
3. Precise REL section names (sec 2/3 ordering of `.rodata`/`.data`).
4. ~~Does the NPDP prototype have symbols?~~ Not documented as having any (see Leads).
5. ~~Does the disc hash match Redump?~~ Yes (redump.org/disc/7133/).
6. REL rebuild quirks [uncertain]:
   - the module name lives in the separate `SonicR.str` (nameOffset 0, nameSize 40)
   - no epilog/unresolved functions
   - Need to confirm that `dtk rel make` reproduces these byte-for-byte.
7. Why are `.ctors`/`.dtors` empty with no `__init_cpp_exceptions`? Possibly a custom runtime or link setup.

## Naming decisions
(none yet)
