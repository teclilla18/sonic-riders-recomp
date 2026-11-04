# PS Vita port: research (2026-11-03)

**Investigation.** Is a PS Vita port of the Sonic Riders recomp realistic? From public guides, GitHub pages and
news, read 2026-11-03; nothing here was tested by us. Estimates are marked [uncertain]. The user owns a modded
PCH-2000 (Slim); its firmware and installed plugins are not known yet. Index: [REFERENCE.md](../REFERENCE.md).

## Verdict (short)

- **Memory: fine.** Guest MEM1 (28 MiB backed) + ARAM 16 MiB + ~20 MB of code fit easily in the Vita's
  512 MB RAM / 128 MB CDRAM. One change needed: the guest window is a 512 MiB sparse reserve (locked cache at
  `0x20000000`); the Vita has no cheap sparse reservation, so the locked cache must move next to MEM1.
- **CPU: the blocker.** Estimated game thread 50-90 ms per step at 444-500 MHz [uncertain] (see CPU below). The
  game's own 30 fps mode needs <= 33 ms per step; 60 fps is out of reach. Needs 1.5-2.7x less game-thread work.
- **GPU: second blocker.** Busy race views send 5,000-7,000 draws per frame; vitaGL-based ports report per-draw CPU
  cost as the main limit (JK2VITA dropped vitaGL for raw GXM). Our draws would need raw GXM, batching and
  precompiled specialized shaders (no ubershader on an SGX543MP4).
- **Closest precedent:** Xita (Xbox → Vita static recomp, Halo CE) reached 12-20 fps on hardware in September
  2026 and was closed; the Halo work moved to a native decompilation port that reaches "up to 30 fps". Static
  recomps of 2001-era console games on Vita are, so far, slow.
- **Conclusion:** a Vita build that boots and plays slowly is plausible; a full-speed one is not likely without
  large game-thread savings. Worth one cheap probe (below) before any port work.

## 1. Jailbreak / homebrew status (late 2026)

- Last firmware is 3.74. **HENlo** (browser chain, `jailbreak.psp2.dev`) runs HENkaku on 3.65 / 3.68 / 3.74
  without a PC; it does not survive a reboot. Older firmware updates to 3.74 first. (vita.hacks.guide, Oct 2026.)
- **Ensō** (permanent CFW, loads at boot) needs 3.65 or lower; **VitaDeploy** "Quick 3.65 Install" downgrades and
  installs it (preset R+YAMT). iTLS-Enso adds TLS 1.2 for online use.
- Storage: **YAMT** (SD2Vita, 3.60/3.65 + Ensō only) or **StorageMgr** (psvsd, PS TV USB). PCH-2000 has 1 GB
  internal `imc0:`; PS TV and PCH-2000 count as the same as the 1000 for homebrew.
- Plugins load from `ur0:tai/config.txt` (taiHEN): `*KERNEL` for `.skprx`, `*main`/`*TITLEID` for `.suprx`.
- **libshacccg.suprx** (the shader compiler from the PSM runtime, extracted and decrypted on the console) is needed
  by any vitaGL homebrew that compiles shaders at run time (installed via VitaDeploy or the ShaRKF00D app).

## 2. Toolchain

- **VitaSDK** (GCC for ARMv7-A, hard-float by default; a soft-float `vitasdk-softfp` also exists, used by
  repentogxm). CMake helpers build `eboot.bin` (`vita-elf-create` → `vita-make-fself`), `param.sfo`
  (`vita-mksfoex`) and the `.vpk` (`vita-pack-vpk`, a zip). Install: VitaShell opens the `.vpk`; during
  development, upload only `eboot.bin` into `ux0:app/<TITLEID>/` over FTP.
- `kubridge` (kernel helper) gives user apps extra memory mapping and exception handlers; repentogxm loads its
  recompiled code at a fixed address through it.
- **Overclock: PSVshell** (Electry; PSVshell+ fork): CPU steps up to 500 MHz, GPU (ES4) up to 222 MHz, bus 222.
  An app may also set clocks itself (`scePowerSetArmClockFrequency(444)` is the common, Sony-allowed maximum).
  500 MHz reported fine by users but drains the battery; LOLIcon (2018) reached 494 MHz. Treat **444 MHz as the
  safe default**, 500 as optional; state clocks with every hardware number (as for the Switch).
- Crash dumps: `ux0:data/*.psp2dmp`, read with `vita-parse-core` against the ELF.

## 3. Graphics

- **GXM** is the native API: command lists, explicit memory (CDRAM/phycont), shaders as `.gxp` binaries.
- **vitaGL** (Rinnegatamante): OpenGL ES-like layer on GXM. Shaders: Cg compiled at run time by libshacccg (via
  vitaShaRK), experimental GLSL→Cg translation (`HAVE_GLSL_SUPPORT=1`), file shader cache
  (`HAVE_SHADER_CACHE=1`), or precompiled GXP. Pools (CDRAM, RAM, phycont, circular/immediate, uniform) are
  sized at init; Celosia shrank them to free 29 MB and gives each render target a shared scratch depth buffer.
- No published draw-call ceiling. Port write-ups: JK2VITA moved off vitaGL to raw `sceGxm` (static world vertex
  buffers in GPU memory, only indices per frame, uniforms resolved outside the per-draw path); Ship of Harkinian
  needed an "over-optimized" vitaGL setup and got up to 300% from optimisations; repentogxm hits 33-34 fps at
  ~78 draws/frame in dense rooms (CPU-bound, Lua). Disabling vitaGL's vblank wait and MSAA helps; scissor avoids
  overdraw; Zenonia went 20-24 → 30 fps partly by raising GPU/bus clocks.
- **For our GX shader:** the ubershader (runtime TEV loops) is far too heavy for the SGX. We would need the
  specialized-shader path only, translated to Cg (our GLSL rules already avoid dynamic indexing, which helps), and
  compiled ahead: either libshacccg at first run with a persistent cache, or a prebuilt set of GXP keyed by the
  shader key (the keys are game-derived facts: generate them on the user's console, never ship them).
- Resolution: GameCube native 640x480 is below the screen's 960x544; render at ~640x448 (or 720x408) and scale.

## 4. Existing ports and recomps on Vita

| Project | Kind | Status / numbers | Memory / tricks | Game files |
|---|---|---|---|---|
| [Xita](https://github.com/Xita-Project/xita) | Xbox → Vita static recomp (x86 → C), D3D8 + DirectSound runtime | Closed; Halo CE campaign on hardware, 12-20 fps (Sep 2026 tester builds) | NV2A combiners → shaders by generator; function-by-function checks against the original | own copy |
| [Halo CE Vita](https://www.gamebrew.org/wiki/Halo_CE_Vita) (BirchWoodGod) | native port from the OpenCE decompilation | v1.1.0 (2026-10-15), campaign "up to 30 fps" | D3D → GXM translation | own copy |
| [celosia](https://github.com/HenryKun55/celosia) | Shadow of the Colossus (PS2) static recomp (PS2Recomp) + decomp | Vita build runs, 18-25 fps in Vita3K only; 30 fps cap | patched vitaGL, pools resized, hot-code order file, RAM-ring logging (per-line log writes cost ~5 ms/frame), "sacrifices" | own disc to `ux0:data/celosia/disc/` |
| [repentogxm](https://github.com/0xl0cal/repentogxm) | Isaac Repentance, PC x86 → C → ARM | v0.1.1-alpha (2026-09-07); 60 menus, 33-60 in game | 81 MiB heap, code at fixed address via kubridge, vitaGL + OpenAL, needs libshacccg | own PC copy (hash-checked) |
| [Ship of Harkinian Vita](https://github.com/Rinnegatamante/Shipwright), 2ship2harkinian | N64 decomp ports | released; MM 1.0 in Apr 2026 | libultraship memory overhead, heavily tuned vitaGL | own ROM → OTR |
| [AstraRecompish](https://github.com/ChatProductions/AstraRecompish) | PS2 → Vita recomp research | no playable retail game | | |
| [vitarecomp](https://github.com/Tzeentchnet/vitarecomp) | the other way: Vita ARM → C for PC | not a Vita target | | own dump |

No GameCube or PowerPC static recomp targets the Vita (none found). Every project above makes players supply their
own game files, as we do.

## 5. CPU estimate [uncertain]

- Vita: 4x Cortex-A9 (3 usable by apps), 333 MHz default, 444 max allowed, 500 with PSVshell; VFPv3 + NEON
  (single precision only in NEON), small caches, slow LPDDR2.
- Our game thread: 13.5 ms/step on an overclocked Switch (A57 at 1785 MHz, perf test 2026-10-30), 15-21 ms per
  frame at stock 1020 MHz in busy views; ~5 ms on the S23 Ultra; 2.4 ms on the PC. It includes GX FIFO decode and
  draw translation, not only game logic.
- A Cortex-A57 does roughly 1.6-1.9x the work of an A9 per clock (DMIPS/MHz and SPEC-like figures). From the stock
  Switch numbers: 15-21 ms × (1020/444) × ~1.7 ≈ **60-82 ms at 444 MHz**, ~**52-73 ms at 500 MHz**. Memory latency
  on the Vita likely makes it worse.
- **30 fps** with the game's own 30 fps mode (`8021BB7C`, one step covers two frames) needs <= 33 ms per step:
  1.6-2.5x less work. 60 fps is out of reach. Sources of savings to check: move GX FIFO decode/draw building off the
  game thread to cores 1-2 (Celosia and repentogxm both split threads this way), the recompiler's ARMv7 code quality
  (32-bit host: 64-bit helpers in `ppc.h` cost more), fewer particles/effects, 1P only.
- 2-4 player local split screen: not realistic (4P already needs 17-19 ms on the overclocked Switch).

## Controlling the Vita from this PC (like switch-remote)

- **vitacompanion** (devnoname120): user + kernel plugin. FTP on port **1337**; commands on TCP **1338**:
  `launch <TITLEID>`, `quit <TITLEID>|all`, `press` / `release` (buttons, sticks, touch), `screen on|off`,
  `nosleep`, `reboot`, `promote <dir>`, `wait 100ms`, chained with `;`. Example:
  `echo 'press cross; wait 100ms; release cross' | nc <vita-ip> 1338`. Upload: `curl -T eboot.bin
  ftp://<vita-ip>:1337/ux0:/app/<TITLEID>/eboot.bin`. This covers build-upload-launch-press, as switch-remote does.
- **Screenshots:** vitacompanion has none. Options: our own captures (`SR_SCREENSHOT`-style BMPs written to
  `ux0:data/...`, pulled over FTP: the project rule anyway); **udcd-uvc** (xerpi; v1.8 updated 2026-09-16 with
  audio) makes the Vita a USB webcam: the PC can grab frames with ffmpeg over the PCH-2000's USB port; VITA2PC
  streams over Wi-Fi.
- **Logs:** write to a RAM ring and flush to `ux0:data/<game>/log.txt` (Celosia lesson), pull over FTP; or a UDP
  log socket to the PC.
- A small MCP server (like switch-remote) could wrap FTP + port 1338 + USB capture. Not built yet.

## The maintainer's test Vita (checked 2026-11-03 over VitaShell USB, read-only)

- PCH-2000, firmware 3.65 with Ensō; `ux0:` is a ~120 GB card through YAMT (~13 GB free).
- Present: libshacccg (extracted with ShaRKF00D to `ur0:data/libshacccg.suprx`), iTLS-Enso, YAMT, PSM runtime
  driver, AutoPlugin 2, VitaGrafix, VitaShell, crash dumps in `ux0:data/` (so core dumps are on).
- `ur0:tai/config.txt` (read over VitaShell FTP): **kubridge**, **PSVshell**, **lolita500** and NoPowerLimits are
  loaded (two overclockers at once: the probe must log the clocks it actually got with
  `scePowerGetArmClockFrequency` / `scePowerGetGpuClockFrequency`); pngshot (PNG screenshots); kuio. On the card
  but not loaded: **udcd_uvc** (USB screen capture). **vitacompanion: not installed.**
- VitaShell FTP (SELECT, port 1337) works over Wi-Fi; its connection drops now and then while the Vita stays on:
  retry instead of assuming it is off.

## Suggested probe build (like Android milestone 1)

Goal: measure, on the user's PCH-2000 at 444 and 500 MHz, whether 33 ms per step is reachable, before any renderer
work. Run on hardware only (Vita3K is not a stand-in: it skips DRAM latency and aliases render targets, per Celosia).

1. **V1a, headless core:** VitaSDK `.vpk` with the recompiled game + devices, null GX (FIFO decoded, no draws),
   locked cache moved inside a small window, game data read from the user's own files under `ux0:data/sonicriders/`.
   Replay the `SR_DETCLOCK` det route; report game-thread ms/step and a memory hash per frame compared to the PC
   (also proves ARMv7 correctness).
2. **V1b, GXM microbench:** 7,000 small draws per frame with one precompiled specialized shader, plus libshacccg
   compile time for 50 specialized shaders. Report CPU submit ms and GPU ms at GPU 222 MHz.
3. **Gate:** continue only if V1a is <= ~45 ms/step at 500 MHz (headroom for savings) and V1b submits 7,000 draws in
   <= ~15 ms on a second core. Otherwise stop and record the numbers.
4. Prerequisites still open: vitacompanion (to install, with the user's OK) for launch and button presses.

## Sources (read 2026-11-03)

- vita.hacks.guide, updating firmware 3.74 / HENlo: <https://vita.hacks.guide/updating-firmware-(3.74)>
- RetroHandheldHQ Vita homebrew guide (updated Oct 2026): <https://retrohandheldhq.com/posts/ps-vita-homebrew-guide/>
- consolemods: HENkaku <https://consolemods.org/wiki/Vita:HENkaku>, PSVshell
  <https://consolemods.org/wiki/Vita:Overclocking_(PSVshell)>, libshacccg
  <https://consolemods.org/wiki/Vita:Installing_Libshacccg.suprx>
- PSVshell <https://github.com/Electry/PSVshell>, PSV-Shell-Plus <https://github.com/darkelf44/PSV-Shell-Plus>;
  r/vitahacks overclock threads (2024-2026, anecdotal)
- vitaGL <https://github.com/Rinnegatamante/vitaGL>, vitaShaRK <https://github.com/Rinnegatamante/vitaShaRK>
- JK2VITA releases (raw GXM) <https://github.com/NDRWhun/JK2VITA/releases>; Client3 Vita PR
  <https://github.com/lesleyrs/Client3/pull/11>; Zenonia 3 port
  <https://github.com/MetalSyntax/zenonia3-psvita-port/releases/tag/v01.20>
- Xita <https://github.com/Xita-Project/xita> (tester build 2026-09-18); Halo CE Vita
  <https://www.gamebrew.org/wiki/Halo_CE_Vita>, Generation Amiga 2026-09-09
  <https://www.generationamiga.com/2026/09/09/halo-combat-evolved-is-being-ported-to-playstation-vita/>
- celosia <https://github.com/HenryKun55/celosia> (round 35 on 2026-10-30); repentogxm
  <https://github.com/0xl0cal/repentogxm> (v0.1.1-alpha, 2026-09-07)
- Ship of Harkinian Vita <https://psx-place.com/threads/release-ship-of-harkinian-vita.47750/>; Majora's Mask
  Vita, Time Extension 2026-04 <https://www.timeextension.com/news/2026/04/n64-classic-zelda-majoras-mask-gets-unofficial-port-for-the-ps-vita>
- AstraRecompish <https://github.com/ChatProductions/AstraRecompish>; vitarecomp
  <https://github.com/Tzeentchnet/vitarecomp>
- vitacompanion <https://github.com/devnoname120/vitacompanion>; udcd-uvc <https://github.com/xerpi/vita-udcd-uvc>,
  v1.8 with audio <https://www.gamebrew.org/wiki/UDCD_UVC_Vita_with_Audio_Streaming>; VITA2PC
  <https://gamebrew.org/wiki/VITA2PC>
