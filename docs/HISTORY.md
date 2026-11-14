# History

Every dated session and hardware-test note, oldest first (2026-10-16 to 2026-10-25: [archive/history-2026-10-16_to_25.md](archive/history-2026-10-16_to_25.md)). New entries go at
the end. Don't read this file whole: `grep -n '^## ' docs/HISTORY.md` lists the entries; grep a date, an issue
number (`#11`), an address or an `SR_` variable to find the story behind it. Current status: ../STATE.md; durable
facts: REFERENCE.md (index of docs/notes/).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.
## 2026-10-26: GPU cost at stock clocks is the vertex shader (XF memory reads)
- **Dynamic resolution did not help [confirmed, hardware]:** at 0.5x internal resolution race frames still took 18-20 ms of GPU time, and the picture was much worse. Removed (with Auto's settle-on-30), see the commit "Remove dynamic resolution".
- **GPU cost test (`SR_GPU_AB=1`, render_deko.c) [confirmed, hardware, stock clocks, 1x, handheld GPU 384 MHz]:** per-frame GPU timestamps plus vertex/fragment shader invocation and primitive counters (`dkCmdBufReportCounter`). Race, ~25-35k vertex shader runs and ~2M fragment shader runs per frame: normal 11.7 ms; vertex shader with positions only (no lighting, no texgen) 5.2 ms; flat-colour fragment shader 10.9 ms; both 3.4 ms; no draws 1.1 ms. So about half the GPU time is the vertex shader's lighting/texgen, not pixels. Eden reports zero for all counters.
- Why [likely]: every XF memory word (matrices, lights) was two dependent loads: the chunk table (uniform buffer) and then the chunk pool in a storage buffer (global memory, long latency), with ~5,000 small draws a frame (a few vertices each), so latency is not hidden.
- **Fix:** each state's vertex shader gets its XF memory as one uniform buffer (`XF_UBO` in gx_common.glsl; `xf_image` copy in render_deko.c): chunks 0-3, 16-17 and 20-25 (0x300 words, 3 KB) packed per chunk table, read through the constant cache; matrix rows are one vector load. The shader is 53 KB instead of 118 KB (same 54 registers). The PC (GL) renderer keeps the chunk table + buffer texture. Picture identical in Eden (demo races, split screen).
- `SR_GPU_AB=1` now alternates every frame between the new shader and the old storage-buffer one (same picture) and logs the GPU time of each every 600 frames. Hardware numbers still to come.
- **Hardware result (hw32, stock clocks, 1x, 384 MHz) [confirmed]:** with `SR_GPU_AB` alternating per frame, races took 9.3-11.6 ms of GPU time with the uniform-buffer shader vs 12.9-15.6 ms with the old one (-27% in every race block); menus unchanged.
- **Next limit at 60 fps: the GP thread [confirmed]:** in the heaviest race views (5,000-6,000 draws, ~47k vertices) it was busy 17.9-19.1 ms per frame on core 2 (vertex decode ~7 ms, the rest of draw processing ~6 ms, command parsing ~6 ms), with game work 12-13 ms on core 0. The render thread's core 1 idles while it waits for the GPU.
- **Vertex decode helper (gx_render.c, `SR_DECODE_HELPER=0` turns it off):** draws' decodes are queued (FastPlan + a copy of the stream bytes) for a helper thread on core 1, below the render thread in priority; the GP thread decodes whatever is not started at draw done/tokens (BP 0x45/0x47/0x48), at the frame's submission and before the vertex array grows, so guest arrays are read no later than before. PC and Eden: same picture; Eden helper share ~30% of the decode, PC ~45%. Hardware numbers still to come (`[perf] draw processing parts: ... (+ X on the helper thread)`).
- **Hardware result with the decode helper (hw33, stock clocks, 1x, 384 MHz) [confirmed]:** the helper does 70-85% of the vertex decode (e.g. 7.4 of 9.2 ms); GP thread busy fell from 18-19 to 8-13 ms per frame in 4,000-5,000-draw views. **30 fps mode: a steady 30.0 in every race block** (GP 6-13 ms, GPU 7-15 ms, game work 10-17.6 ms per drawn frame, i.e. per two steps). **60 fps (original): 60 in views up to ~4,000 draws, 31-47 fps in 5,000-6,400-draw views** (user: "a trainwreck"; that mode also slows the game when late). There the game thread's own work is 13-19 ms per frame (game core samples ~25-30% in the idle loop `8000D460` OSEnableInterrupts from SelectThread, i.e. waiting for the next retrace after missing one; GX FIFO path ~4%), with GP 12-17 ms, GPU 12-15.6 ms, and core 1 (render thread 10-14 ms incl. waits, helper 8-11 ms, audio 1.7 ms) also full. [likely] Holding 60 at stock in the heaviest views needs the game thread's work cut by ~20% (recompiled code), as well as the GP thread and core 1; 30 fps is solid.

## 2026-10-26: register summaries at calls (game-thread CPU)
- Hardware (hw33, stock): in the busiest 60 fps views the game thread's own work is 13-19 ms per frame; the profile is flat (game code; the hottest are the SDK's PSVEC/PSMTX routines at 8004C58C-8004D09C, a few % each).
- Call overhead in the generated code: before a call the caller wrote back every register it had changed, after it re-read every volatile register it uses (e.g. a PSVECNormalize call site stored 14 and reloaded ~40 values).
- **Register summaries** (leaflocal.py `summarize`/`solve_summaries`, gcrecomp.py `compute_summaries`; `RECOMP_SUMMARY=0` off): per function, every GPR/FPR it or anything it calls/jumps/falls into mentions. A call to a summarized function writes back only dirty registers in the summary (the rest stay dirty in locals) and re-reads only volatile ones in it. **r1 is always in a summary**: interrupts delivered at a callee's entry put their frame just below the struct's r1 (sched.c make_irq_cpu). No summary (full sync as before): indirect calls (ppc_dispatch), native replacements, instruction hooks, unknown instructions, rfi/mtmsr/sc/trap, NO_ABI context switches, or any callee without one. Falling through counts only if the last instruction is not b/blr/bctr/rfi. 2,310 of 5,208 functions get one. Safe because each guest thread has its own Cpu and interrupts run on a copy (sched.c).
- Checking build `RECOMP_SUMMARY_CHECK=1` (ppc.h `ppc_sum_poison`/`ppc_sum_check`): around every summarized call, every register outside the summary (but r2/r13) gets garbage in the struct and is checked/restored after. PC: attract demo (title, 1P and split-screen races) and a scripted menu route (menus, loading, a race, pause menu): no reports, pictures correct [confirmed]. Eden (normal build): races correct. Hardware speed not measured yet.
- **More per-access/entry savings (same session):** ppc.h declarations get hidden visibility (`#pragma GCC visibility push(hidden)`): the PIE build read g_ppc_cur_fn / g_ppc_cur_lr / g_ppc_trace / g_core_preempt through the GOT at every function entry (one extra dependent load each). `PPC_IS_MMIO` is now `(ea >> 27) == 0x19` (same range 0xC8000000-0xCFFFFFFF): the old `ea - 0xC8000000 < 0x08000000` needed a 32-bit constant and an add per access. Switch game code text 20.50 -> 19.09 MB. gx_pipe_write has a fast path for plain 4-byte pipe stores (no BP prefix pending, no burst completed): most of the ~50,000 stores a busy frame makes skip three calls. ppc_unimplemented/ppc_trap (rt_fatal) no longer deny a register summary.
- **Measuring these locally does not work [confirmed]:** PC (exact host profiler, busy samples over VI fields 8000-16000 of the attract demo) and Eden (game-core busy share per 10 s, game work per drawn frame) show the same numbers with and without the changes (within ~1%). The PC/JIT costs of extra loads/stores/conversions are small; the Switch's A57 at 1 GHz is the only meaningful measure. The hardware `[profile]` is "last function entered", so a caller's work after a call is charged to the callee: the PSMTX/PSVEC routines (8004C58C PSMTXConcat?, 8004CF6C PSVECNormalize, 8004D00C PSVECCrossProduct, 8004D048 PSVECDistance, 8004CAE0, 8004CFC8 PSVECMag) look like 10-23% of busy race time there but are ~2% on the PC's exact profile. Native versions were considered and left: bit-identical results need the same frsqrte/round25 sequence, so only the call overhead would go (~1%).
- Remaining larger idea (not started, risky): keep FPRs that only ever hold single-precision values as `float` locals (singleflow already knows most operands), removing the double<->float conversions of every single-precision op and load.
- **Single-precision ops in float (singleflow.py `RECOMP_FLOAT=1`, off by default) [confirmed identical, no gain]:** `to_single(A op B)` (+ - * /, both halves known single) as `(double)((float)A op (float)B)` is bit-identical (double > 2*24+2 bits; `RECOMP_FLOAT_CHECK=1` on PC: demo + menu route, 7,379 sites, no differences), but GCC keeps the register locals as doubles: one hot file had 5,328 fcvt vs 5,263 without. Needs float-typed locals to pay off.

## 2026-10-26: specialized shaders stopped matching after the second demo race
- **Hardware (hw34, hw35, stock clocks, handheld 384 MHz) [confirmed]:** GPU time per frame jumped about a minute in (second attract-demo race) and stayed high: the same menu screen (29 draws, ~3M fragment shader runs) took 2.8 ms before and 41.5 ms after; races 9-13 ms before, 37-50 ms after (18-20 fps). Not heat or charging (hw35 was on battery). The `[deko3d]` line showed why: "specialized shaders for 1680 of 1680 states" before, "0 of 45070" after: every state fell back to the ubershader.
- Cause: the specialization key (gx_shader_key.c) included each stage's konst color/alpha selects (BP 0xF6-0xFD) whenever the stage existed. Some material sets stage 0's konst alpha select (0xF6 0x000064 -> 0x003864) and the game never clears it, so every later key differed from the table. Same for indirect references/scales (0x25-0x27) of indirect stages not in use.
- **Fix:** a stage's konst color select counts only if one of its color inputs is KONST (14), the alpha select only if an alpha input is KONST (6); 0x25-0x27 keep only the bits of the first `nind` indirect stages. The shader reads those bits nowhere else, so specialized shaders are unchanged for the states they cover. gen_shaders.py applies the same masks to recorded key files and recomputes the hash (checked: same hashes as gx_frag_key for all 38 old keys), so old key files still work. The 30 new keys hw34/hw35 recorded collapse to 3 new ones (table 38 -> 32 entries).
- **Hardware result (hw36, stock, battery, attract demo past the point where hw35 broke) [confirmed]:** specialized shaders for 2,563,268 of 2,563,268 states; GPU 7-13 ms per race frame for the whole run (hw35: 26-47 ms). In 60 fps mode, the busiest views now sit at 30 because of the game thread: game work 15-21 ms per drawn frame at 5,000-7,000 draws (GP 10-19 ms, GPU 11-17 ms). Game work at matching draw counts is about the same as hw33/v0.3.1, so the register summaries / visibility / MMIO changes show no clear gain on hardware either [likely, few samples].
- [likely] This also hurt the hw33 "60 fps trainwreck" result and any long session since the table was built; earlier per-change hardware comparisons that ran past the first demo race are suspect.

## 2026-10-26 (night): blue tint (#11) investigation, in progress
- Fresh Dolphin reference: `-b -e <iso>` with no input, unthrottled, AVI dump (mpeg4 40 Mbit/s, 1x): the first attract-demo race (Metal City) is at Dolphin frames ~8800-12300 (countdown at ~8800). PC runs are not frame-deterministic (the demo starts at different presented frames between runs), so compare by averages or by timer, not by frame number.
- Average colours over the race [confirmed]: whole scene PC 90/106/121 vs Dolphin 91/106/119 (same); area around the player character PC 98/111/120 vs Dolphin 113/123/133 (PC ~13% darker, slightly bluer). Matched pair (timer ~4.5 s): PC Sonic looks washed out, red shoes pinkish-blue, gloves bluish, as if a see-through cyan layer were drawn over him; Dolphin's are vivid.
- `SR_SKIPSAME=8400,250` (one PC frame): removing draws 3500-3749 removed a translucent cyan/green layer over the lower screen that also hid rings and COM markers behind it (Dolphin shows grey track there). Sonic himself was in draws 4750-4999, the board in 4500-4749. Next: `SR_SKIPSAME=8400,25` with `SR_GXDEBUG=8400` (block cap raised to 400 in render_gl.c) to find the exact draw and its state (blend, z, texture: likely a copy texture or the speed-blur overlay), then compare with Dolphin.
- 2026-10-29: `SR_SKIPSAME=8400,25` (4172 draws that run). Largest change: block 164 (draws 4100-4124), which holds the speed-blur overlay: one 18-vertex draw sampling the half-size RGB565 copy of the previous frame (copy exec 010243, 640x480 -> 320x240 in a 512x256 texture; color = tex x vertex colour, alpha = vertex alpha, SRCALPHA/INVSRCALPHA), followed by the one EFB->texture copy of the frame, then the HUD. Removing it makes the scene sharper and more vivid. The copy texture itself looks right (dumped with `SR_GLTEXDUMP=114C6A0`). Blocks 128-131 (draws 3200-3299) are additive particles (32x32 textures, ONE/ONE and SRCALPHA/ONE); removing them lowers G/B a little (the cyan trail/jet). [likely] the tint is the overlay and/or these particles, not fog: fog-amount view (`SR_TEVDEBUG=10`) shows little fog near the camera. [uncertain]
- Caveat [confirmed]: `SR_SKIPSAME` repeats re-run the frame's EFB copy, so the blur overlay feeds back from repeat to repeat (single-draw skips show a decaying difference 6.6, 3.0, 1.5, ... from the first repeat). Differences after the overlay draw are only meaningful relative to each other. `SR_SKIPSAME=N,STEP,FIRST` now starts at draw FIRST.
- Next: make the repeats restore the copy texture (or skip the copy) so single-draw skips are clean; then check the overlay's vertex alpha over the whole quad strip against Dolphin (Dolphin frame dump, same kind of scene), and the particles' texture format/alpha decode.

## 2026-10-29: #11 at Player Entry; scripted input for Dolphin; Switch driven over Wi-Fi
- **Reference frames from the original game in Dolphin, scripted** [confirmed]: hand-made .dtm movies are ignored by this Dolphin (no error, no input), so `tools/dolphin_dsu_input.py` is a DSU (cemuhook) server that Dolphin's DSU client reads as a controller (timed presses; device `DSUClient/0/Script`; Start = Options, A = Cross; face buttons need the analog bytes too). Route to Player Entry from an empty memory card: Options every 1.5 s for 20 s, then Cross every 2.5 s; Player Entry with Sonic at Dolphin frame ~2590. `GFX.Settings.DumpTextures` via -C dumped nothing (game or FIFO).
- **Player Entry** (PC route: START at 300/600/900/1200, A at 1500..3300 every 250-300 fields; Player Entry from ~3200, timing drifts): every draw is a 6-vertex quad with lighting off (channel ctrl 0x441); Sonic is a CI8 256x512 sprite with an RGB5A3 palette (texture 0xC6A580), not a lit model. Whole screen [confirmed]: Switch (switch-remote screenshot) 119/130/145 vs real game 119/130/146; Sonic's blue areas Switch 33/75/144, PC 35/80/148, real 26/65/135 (~8 lighter), reds equal. Dolphin replaying our frame (`SR_DFF=4000,3`) draws it like we do. So at Player Entry we match the real game within a few percent; the slight lightening of the sprite is unexplained (palette snapshot timing or a pulsing highlight are possible). [uncertain whether this is what the user sees]
- Hardware is now reachable from the PC (switch-remote MCP: buttons, screenshots, FTP); Player Entry screenshot taken that way on the overclocked Switch.
- **Found it [confirmed, hardware]: the tint is the Switch's vertex lighting.** Player Entry, two more A presses ("Are you ready?") shows the lit 3D model: on the Switch white gloves/shoes were cyan and skin olive; PC and the real game (Dolphin, same screen via tools/dolphin_dsu_input.py) were correct. Steps (each a hardware run driven over Wi-Fi with switch-remote; test builds via the new `SR_DK_DEBUG_TEV=<view>` build-time variable of tools/embed_shaders.py, which fixes a SR_TEVDEBUG view on in the deko3d shaders): `SR_NOSPEC=1` still teal (not the specialized shaders); `SR_GPU_AB=1` teal in both XF paths; view 2 (stage-1 texture) black, so not the shine texture; view 1 (lit colour 0): PC grey, Switch teal with r/g ~0.44; temporary views showed the material, ambient and light-colour registers correct on the Switch and the material white, so the error was inside `lights()`; a view returning `vec4(lights(...), flag)` also got a wrong `.a`, i.e. uam mixed vector components there.
- **Fix (gx.vert `lights`):** returns the clamped vec4 sum (callers take `.rgb` / `.a`; no `alpha ? vec3(sum.a) : sum.rgb`), and the loop accumulates only the lights' part in a vec3 + float with no `continue`, the ambient added after the loop. The vec4 return alone was still teal on hardware; the loop change fixed it (so the miscompile involved the vec4 running sum seeded with the ambient and/or the `continue`). [confirmed] on hardware (overclocked): "Are you ready?" Sonic matches Dolphin (white gloves, red shoes, tan skin); race at Metal City: Shadow and Cream correct, 59.9 fps at the countdown. PC unchanged (same screen). GPU cost of the new loop not measured (no visible fps change).
- Rule of thumb added to the uam list: in vertex shaders, avoid loop-carried vec4 accumulators seeded from a parameter, and `continue` in loops that accumulate vectors.
- Other: Switch env.txt lines may be up to 1024 characters (SR_INPUT scripts); SR_INPUT itself is PC-only, so Eden can't follow scripted routes yet. Eden crashes (divide by zero) when started minimized; `-WindowStyle Hidden` works.

## 2026-10-29: setup fails under a non-English Windows user name (GBAtemp report)
- **Report [confirmed from the log]:** the player's Windows user folder has non-English letters (a Turkish dotted capital I and a c-cedilla); the setup reached step 6 and every ninja command failed with `CreateProcess failed: The system cannot find the file specified`, with those letters mangled in the printed tool paths. ninja passes commands to CreateProcessA (the 8-bit ANSI code page), which can't carry such letters; the tools folder defaults to `%LOCALAPPDATA%` (under the user name), and the release folder was under Downloads.
- **Fix:** setup.ps1 uses `%SystemDrive%\SonicRidersTools` when the default tools path has non-ASCII (as it already did for spaces) and stops at the start with a clear message when the release folder's path has non-ASCII (the compilers run there; not tested whether they would cope); configure.py's `cmd_path` prefers ASCII paths (a relative `../../AppData/...` avoids the user name) and configure refuses to write a build.ninja that still has non-ASCII. README-FIRST and docs/INSTALL.md say to extract to e.g. `C:\SonicRiders`. Not tested on a machine with such a user name. Workaround for v0.3.3: extract to `C:\SonicRiders` and run `setup\setup.ps1 -Target switch -ToolsDir C:\SonicRidersTools`.

## 2026-10-29: online play, phase 1 (determinism): SR_DETCLOCK
- Design and constraints (one player per machine; PC-Switch cross-play) in docs/ONLINE.md; its "Determinism: findings" section is the detailed log of this work.
- Sources of run-to-run differences found, in order [confirmed with SR_RAMHASH / SR_RAMDUMPAT two-run diffs]: the host clock read by the game (OS start time, alarms, disc timing); disc completion timing (a REL chunk loaded by frame 2 in one run only); retraces landing mid-frame (the game's frame multiplier 0x8021BBF4 was 3.0 in one run, 1.0 in the other, after slow frames); background loading finishing on different frames; audio running on the host clock (the pre-race intro waits on audio).
- All of these have a deterministic replacement under SR_DETCLOCK=1 (cpu.c, di.c, si.c, input_sdl.c, vi.c, dsp.c, sched.c, frame_pacing.c), off by default; normal play unchanged ([confirmed] PC race at 60 fps; Switch build compiles, not run).
- State: first frames identical; race scene starts within 1 frame between runs; the race diverges later than before but not yet zero. See docs/ONLINE.md "Next".
- Remaining nondeterminism before races: the loading path's frame end busy-waits on the retrace count in the game's 30 fps mode (not an EFB wait, as first written); handled now. Race scene starts on the same frame in both runs; the race itself still differs intermittently. Details in docs/ONLINE.md.
- 2026-10-29 late: SR_DETCLOCK idle ticks (vi.c det_idle_tick) and SR_RAMDUMPKEY_AFTER. Race scene starts within 0-1 frames; race data identical through a whole race in one pair of runs, not reliably. Assessment and the two ways forward in docs/ONLINE.md.

## 2026-10-30: SR_DETCLOCK reaches whole-run determinism on PC
- User chose "make the race deterministic, sync at the race start" (docs/ONLINE.md way (b)). Measuring race state showed the race only differs because loading and the menus do, so the remaining sources were found and removed instead; the whole run is now deterministic.
- [confirmed] PC, scripted route to a 1P Metal City race with steering in the race: two runs give identical SR_RAMHASH checksums of all 28 MB on every one of 7633 frames (boot to race frame ~2400); six runs (two with SR_GPSLOW=4, a 4x slower GP thread) are identical at the race start. Normal mode (SR_DETCLOCK off): PC race 60 fps; Switch build compiles, not run.
- What was still host-timed, and the fix (all only with SR_DETCLOCK=1):
  - Frame pacing (frame_pacing.c): its skips, "30 when busy" switch and late presents follow the host's timing and change the game's step (8021BBF4) -> bypassed; the game's own frame end runs, including its own 30 fps mode while loading.
  - Retrace handover (vi.c det_raise): only at a quiet point (det_quiet): no interrupt handler running (sched.c g_irq_running), and either the OS idles or the running thread sits in a known wait loop. Code patches mark the loops (patches.txt): the frame end's retrace-count loop 800061C8 (g_det_spin) and the REL streaming loader's eleven disc-data loops in 819594F4 (g_det_lspin). Before, a retrace could land while the loader thread (81A93E80) was decompressing, so loading progress per frame (and the heap layout at the race) depended on the host.
  - The main thread can be "current" while already asleep in VIWaitForRetrace (OSThread.state 4 at +0x2C8): counted as asleep.
  - GPU: draw-done/token interrupts AND the PE token/status registers they change are held and applied at a quiet point, or when the game reads a PE register (after the GP drained) (gx_fifo.c gx_det_deliver). Interrupt handlers can write GX commands while the CPU idles: the write-gather buffer is flushed after delivery when idle (sched.c deliver).
  - Idle ticks (game time passing while everything is blocked): exact condition instead of "frames stopped for 3 fields": OS idle, no handler running, nothing ready, no disc read, no GP work or held PE update, no undelivered interrupt. Allowed from boot (det rules no longer wait for the first frame end).
  - Backstops: the retrace backstop stays away while the GPU has queued work (the first frames wait ~300 ms for the GL context) and gives a running main thread 2 s; the alarm (decrementer) host-clock backstop only fires when game time stands still (vi_det_time_moving).
  - Audio: the per-frame AI quota takes the block size from the game's registers, not the host audio thread's copy. AISCNT and the VI beam position (VCT/HCT) follow game time.
  - Controllers: ports are fixed for the run (port 0, or SR_PADS) instead of live hot-plugging; a gamepad found by SDL a moment later changed the OS controller-type detection (`Type`, 80122014) and moved the menus by a frame.
- Tools: SR_RAMHASH_BLOCKS=<from>-<to>[,..] (hash only those MB, "none" = step/key only), SR_RAMDUMPKEY takes a list; [det] logs: backstop details (idle-condition parts, the thread's call chain logged by itself, sched.c g_det_bt_req), "held for thread" samples, threads running at handovers, alarm backstops.
- Build note [uncertain]: after editing config/GXEE8P/patches.txt, `ninja` twice reported nothing to do; running `python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen` regenerated the code.
- Next: the same check PC vs Switch (cross-play: floating point), then input recording/replay (phase 2).

## 2026-10-30 (later): PC = Switch under SR_DETCLOCK
- [confirmed] Same scripted route (menus, then steering in a 1P Metal City race), SR_DETCLOCK=1, both machines on the same memory card image: all 7700 frames (boot to race frame ~2550) have identical checksums of all 28 MB on PC and on the Switch (overclocked, hashing slows it to ~28 fps, which does not matter in this mode); two Switch runs and two PC runs, all four identical; no backstops. Floating point needed nothing: x86 and ARM gave the same bits for this route. Normal mode unchanged (PC race 60 fps with SR_INPUT through the shared script code).
- What differed, how it was found, the fix:
  - Save data: the game reads its memory card at boot (~frame 102) and the two machines' saves differ -> SR_CARD=<file> (both targets) points both at the same card image. Online play needs the same save on every machine (or a fixed one): an open item for phase 3.
  - GPU speed leaked into the order of interrupts: the retrace/audio handover did not wait for the GPU, so on the slower Switch an audio interrupt came ahead of a draw-done token the PC had delivered (and fast/slow GPUs grouped several token/finish results differently). Now every handover first waits until the GP has done all queued FIFO work and hands its results over (vi.c det_raise; the retrace thread's own delivery also only with the queue empty).
  - The game's idle loop (SelectThread) enables interrupts once and then spins: the idle handover in sched.c ran only on entering idle, and the retrace thread handed things over while idle in its own order. Now, with SR_DETCLOCK, sched.c det_idle stays in the idle loop and hands everything over from the game thread, one kind at a time: GP results (after the queue drained), commands still in the gather buffer, a disc command, then vi_det_idle (audio, the main thread's retrace, idle ticks). The retrace thread only counts due retraces while the CPU idles.
  - The backstop's 10-field limit fired on the Switch's slower boot while a background thread was busy: 12 s while any game thread runs outside the known wait loops.
- Tools: SR_INPUT now works on the Switch (runtime/src/input_script.c, shared; env.txt lines up to 8 KB); RAM dumps go next to the log on the Switch; SR_RAMHASH_SUB=<block> (64 checksums of 16 KB inside one MB); SR_RAMHASH_REF=<other machine's hash log> (compares every frame and dumps RAM where a block first disagrees, SR_RAMHASH_REF_DUMPS, default 3: catches a difference that moves each run); SR_DETTRACE=1 logs every interrupt raised/handed over and every disc command with the game frame and where the game was (two machines' traces show the first event that lands differently; compare the I/D lines).
- Switch notes: the log file is locked while the game runs (FTP read fails) and a read held open at launch made the game skip its log and env.txt; SR_WATCHDOG's exit ends in a system error screen on the Switch (closing from HOME is fine).
- Next: phase 2 (record a session's inputs per frame and replay them; two copies in lockstep on one PC), the save-data rule for online.

## 2026-10-30 (evening): online play phase 2: recording, replay, lockstep on one PC
- New runtime/src/netplay.c: the controllers the game sees each frame (si.c si_det_latch -> netplay_latch) are live, recorded (`SR_RECORD=<file>`), replayed (`SR_REPLAY=<file>`) or exchanged in delay-based lockstep with another copy (`SR_NET=host:[<addr>:]<port>` / `join:<addr>:<port>`, `SR_NET_DELAY` (host, default 4), `SR_NET_TIMEOUT` (default 10 s)). They turn SR_DETCLOCK on (cpu.c ppc_detclock_force). Frames = frame ends (latches), not ppc_detclock_steps(): idle ticks move the step without a frame end. Format, protocol and rules in docs/ONLINE.md.
- Rolling state checksum: each frame end hashes 1 MB block (frame mod 28) (frame_pacing.c ram_block_hash, at the point where SR_RAMHASH hashes). In recordings (replay checks itself) and in every lockstep packet (last 8). Desync -> both machines log it and show a banner (new overlay_set_banner, menu.cpp), session ends, game goes on alone. `SR_NET_TEST_DESYNC=<frame>` flips a byte of DOL code (unused by the recompiled game) to test it.
- Joining checks protocol, build string (g_build_version) and the memory card checksum (exi.c exi_card_hash); in a session card writes stay in memory (exi_card_volatile). Windows: ws2_32 linked (configure.py), SIO_UDP_CONNRESET off; Switch: socketInitializeDefault() only when SR_NET is set (compiles; not run on hardware).
- [confirmed PC] record/replay 7700 frames identical (full SR_RAMHASH), also with SR_GPSLOW=4; lockstep host+joiner on 127.0.0.1 identical all 7700 frames, byte-identical recordings, offline replay of the session identical; desync, connection loss and wrong-save refusal behave.
- [confirmed PC] Determinism fix found by the replays: game memory block 26 (thread stacks) differed for 4-8 frames during loading in about half the runs (whole-RAM checksums of all other blocks equal). SR_DETTRACE showed a PE token handed to the REL loader thread (81A93E80) "just resumed" in one run and in the idle loop in the other. Two causes: (1) host-side hand-overs trusted the loop marks (g_det_lspin / g_det_spin), which stay set while the thread is switched out, so a hand-over could land as the thread came back and left the loop; (2) at a loop, GPU results were handed over only if the GP thread had already drained, so the outcome followed the GP thread's speed. Fix: the loop heads call vi_det_loop_point() (patches.txt) on the game thread, which waits for the GP to drain, then hands over GPU results, or det_raise (disc, audio, retrace); det_quiet() accepts the loop cases only from there (g_det_at_loop). Two parallel recordings (one with SR_GPSLOW=4) identical on all 7700 frames afterwards. The game's results changed from frame 49 on only in block 26 (where handlers' stack frames land), as expected.
- [confirmed Switch] the PC recording (rec of the det route) replayed on the Switch: all 7700 frames' SR_RAMHASH equal to the PC's, replay self-check equal on all 7920 frames, no backstops (so PC = Switch holds with vi_det_loop_point).
- Not done: more than 2 players, a 2P race route for the lockstep test (player 2 is connected but not racing in the current route), own view full screen (phase 3), the save transfer (proposal in docs/ONLINE.md, waiting for the user).

## 2026-10-30 (evening): performance test (SR_PERFTEST, tools/perf_test.py)
- After SwitchWakerHD's perf_test, simpler: `SR_PERFTEST=1` (runtime/src/perftest.c) boots on a fresh card in memory (exi_card_volatile; the player's save untouched), feeds neutral controllers (si.c get_pad) except START at fields 1500-1509 (skips the intro movie), and measures the attract mode's two demo races (found by running with no input: after the title the game plays Metal City 1P, back to the title, then a 2P split-screen race; scene counter 8021BB64 resets per scene), scene frames 240-2039 of each: speed (scene frames/s), steps/s, fps, game-thread ms per step (hle_frame_end exit -> next entry; hle_frame_end now wraps frame_end_body). Writes perftest.json (PC build/, Switch next to the log); PC exits, Switch shows a banner. Normal pacing (not SR_DETCLOCK: its GPU waits would change the numbers).
- tools/perf_test.py run (PC, N runs) / show (perftest.json files, e.g. from the SD card, or records) with --save and --expect (SLOWER when speed/fps -1.0 or game ms +10% and +0.2). docs/PERF.md; records in perf/results/.
- [measured] PC, 3 runs (188 s each): both scenes 59.9 speed/fps, game thread 2.4 ms mean (2.33-2.51 across runs), p95 4.3.
- [measured] Switch (overclocked, frame rate Auto), 1 run via env.txt (switch-remote): demo_1p speed 59.95, fps 58.1, game thread 13.5 ms (p95 16.6, 77 steps > 16.7 ms); demo_2p (Splash Canyon split screen, seen on screen) speed 59.94, fps 56.4, 13.7 ms (p95 18.7, 385 over). Records perf/results/2026-10-30-{pc,switch}.json.

## 2026-10-30 (night): unlock flags; the generated online save
- Save-data rule for online decided by the user (docs/ONLINE.md "Save data for online races"): a generated save with
  everything unlocked, built in memory on every machine, nothing written to anyone's card.
- **The game's save data** [confirmed]: one card file `SONICRIDERS_GAMEDATA_01` (GXEE/8P, 1 block); comment and
  banner/icon first, game data from file offset 0x1440 to 0x1B50. In memory the game keeps it in one struct at
  **81AAD000** (= file offset 0x1460; 0x6F0 bytes; the code forms the address as lis 81AB / addi -0x3000). It lies in
  _Main.rel's data, so its initial values (the new-game defaults) are there from boot (seen at frame 50); on the det
  route the card's copy is applied between frames 1300 and 2000 (after the file screen).
  How found: located the save's bytes in RAM dumps (SR_RAMDUMPAT) before/after the card is read, then a small
  scanner over the recompiled code's instruction comments listed every load/store off 81AAD000, and the readers were
  disassembled (devkitPPC objdump on words rebuilt from build/gen comments). Flag meanings checked by setting them in
  memory at a fixed frame (new SR_POKEAT) and looking at the select screens.
- **Unlock flags** (offsets in the struct at 81AAD000):
  - +0x004..+0x013, 16 bytes: the tracks, 1 = unlocked [confirmed: all 8 track slots and both courses of each
    (e.g. SEGA Carnival / SEGA Illusion) selectable]. A new game has the first five (Metal City .. Sand Ruins).
  - +0x3A0 + c, c = 0..16: the characters, read as "== 1" [confirmed: all 16 Player Entry slots, no "?"]. A new game
    has 0-6 (Sonic, Tails, Knuckles, Amy, Jet, Storm, Wave). 17 entries for 16 slots: the extra one is probably Super
    Sonic [uncertain].
  - +0x348 + i: gear, bit 0 = owned [confirmed by the code (gear selector at 81999080) and by cycling Sonic's gear:
    High Booster, Auto Slider, Powerful Gear, Fastest, Turbo Star, Speed Balancer, Blue Star II, Access, Beginner,
    Accelerator, Trap Gear, Light Board, Slide Booster, Legend]. The selector walks a 54-entry table at 819D55F8 of
    (u16 flag index, s16 character): flag index 0 = a character's own gear, available when that character is
    unlocked; the others use flag indices 1-24, 40-49 and 70-75. Saves also hold the value 2 here (bit 1: unknown,
    perhaps "seen in the shop").
  - +0x002: read as "== 1" for one course in the mode menus (819952E8 and three similar readers) [likely an unlock;
    which one is uncertain]; set too.
  - Not unlock flags: +0x3BA.. time records (0x112C = the 43"96 best lap shown) [likely]; +0x6B8 0x07D0 = 2000,
    perhaps the ring total [uncertain]; missions / story progress (mostly zero in a new game) not mapped.
- **Online save** (netplay.c, exi.c, main.c): `SR_NET`, or `SR_ONLINE_SAVE=1` (offline, with SR_RECORD; the
  recording's flag 2 makes its replay use it), gives slot A a freshly formatted card in memory (`exi_init(NULL)`;
  fixed contents under SR_DETCLOCK: fixed RTC and SRAM) and never opens SR_CARD / the player's card file. The game
  shows its NEW GAME screen and asks to create game data (the det route's A presses answer Yes and still reach Track
  Select at the same frame). `online_unlock()` sets the flags above at every frame end, before the state checksum:
  idempotent with how the game reads them, and it also covers the game resetting the struct on the NEW GAME path.
  The host's memory-card check is gone (protocol 2; exi_card_hash / exi_card_volatile removed; the perf test uses
  `exi_init(NULL)` too).
- New tool: `SR_POKEAT=<frame>:<addr>:<hex bytes>[,...]` writes guest memory at the end of that frame (frame ends
  from 1, as SR_SHOTSTEP); deterministic, unlike SR_POKE (per present, one byte).

## 2026-10-30 (night): phase 3 start: 2P race route, own view, PC vs Switch NaN sign, first PC-Switch session
- **The det route does not reach the race on the online save**: with every track unlocked, Track Select asks for
  the course (Metal City / Night Chase), an extra A, so the old route stops at gear select and its "race" input cycles
  gear. The first lockstep test on the online save (7,700 identical frames) was therefore menus only. New 2P route
  (scratchpad mk2p.py; not in the repo): player 2 joins at Player Entry (2.A), picks Knuckles, both confirm, START at
  4700, race from ~5900; in the race the stick is held forward (SY=100: on foot to the start line, d-pad alone left
  the racers standing) and steers with SX, A taps. [confirmed] lockstep on one PC 8,400 frames identical (~2,400 race
  frames, both players moving), byte-identical recordings, offline replay self-check all 8,580 equal.
- **Own view** (gx_render.c note_split_view / copy[11], render_gl.c, render_deko.c, netplay_view_player, SR_VIEW):
  2P race frame viewports [confirmed SR_GXDEBUG]: 3D in two 640x240 viewports (scale 320/-120, top at y 0, bottom
  at 240, perspective), 2D HUD in full-screen orthographic viewports (the track map straddles the split line). The
  local player's half is shown scaled uniformly (8:3 at a 4:3 display). [confirmed PC screenshot, SR_VIEW=2: menus
  whole, race shows Knuckles' half]. Switch backend compiles, not looked at on hardware yet.
- **PC vs Switch differ by a NaN's sign** [confirmed, RAM dumps at frame 6918 from both]: details in docs/ONLINE.md
  phase 3 status. Tried and reverted: canonicalizing at stf32/stf64/psq stores on x86 (changed real FFF8000000000000
  data at boot: 138 words). Needs the creation-site fix (phase 4). Also seen: x86 vs ARM agree on fctiwz/psq quantize
  of NaN (both 0 / 0x80000000 paths), not verified exhaustively.
- **First PC-Switch session** [confirmed]: Switch `SR_NET=host:7777` (env.txt, with the route's player-1 part in
  SR_INPUT), PC `SR_NET=join:<switch ip>:7777` (player 2). Joined at once; full SR_RAMHASH equal on both for frames
  0-6917; desync found by the Switch at 6919 (block 3, the NaN), session ended on both with the banner; the Switch
  showed player 1's half full width. One `[det] alarm backstop` on the PC at frame ~1743 during a 585 ms PC stall
  outside net_frame (keepalive not called), harmless that time; open item in docs/ONLINE.md.
- Switch-remote notes: HOME tiles wrap (LEFT from the first tile goes to All Software); the game tile asks "Who is
  using the software?" first (A picks the first user); presses can be dropped, so check with a screenshot.

## 2026-11-03: Android port planned (branch `android`); online play paused
- Online work recorded as paused in STATE.md on `main` (phase 3 next steps: PC-PC and Switch-Switch sessions, then
  the phase 4 NaN-sign fix on x86). Branch `android` created from it.
- `.gitignore` and `tools/check_no_game_data.py` refuse Android outputs: `*.apk`, `*.aab`, `*.apks`, `*.idsig`,
  `*.so`, `*.dex`, `*.spv`, keystores (`*.keystore`, `*.jks`), `android/app/build/`, `.gradle/`. [confirmed: guard
  test on sample names]
- docs/ANDROID_PLAN.md from reading the code: SDL2 covers window, audio, input, lifecycle and paths on Android;
  `platform_host.c`'s POSIX branch, `memory.c`'s mmap branch and `netplay.c`'s POSIX sockets carry over
  [confirmed by reading]; `profile_win.c` is Windows only. Recommended renderer: Vulkan ported from `render_deko.c`
  with SPIR-V built from the shared GLSL [likely; to be confirmed by M1's GLES vs Vulkan draw-cost measurement].
  Nothing built or run for Android yet.

## 2026-11-03 (later): Android milestone 1: toolchain, probe APK, GLES vs Vulkan on the S23 Ultra
- Setup `-Target android|all` (`-Abi`, `-Install`, `-AcceptAndroidSdkLicense`; `-Yes` never accepts the license):
  Google's packages straight from dl.google.com (SHA-1 matched Google's manifest, SHA-256 pinned), Temurin JDK 17,
  SDL2 2.32.10 source. `configure.py --target android` hands off to `tools/android_build.py` (no Gradle). The probe
  APK (`tools/android/probe/probe.c`) reports system/CPU/GPU facts, plays a tone, logs input and measures draws.
  [confirmed] setup run end to end; host-mingw and switch still build.
- Emulator (x86_64, headless, WHPX): probe runs; ES 3.2 context refused (EGL_BAD_CONFIG), so the probe falls back
  to 3.1/3.0. [confirmed]
- **S23 Ultra, Android 17** [confirmed, one run]: GLES 8.06 ms vs Vulkan 2.00 ms CPU per frame of 7,000 draws ->
  Vulkan is the renderer. dualSrcBlend, EDS1/2, push descriptors, dynamic rendering present. Swapchain transform
  ROTATE_90 (pre-rotation needed). 120 Hz panel (8.3 ms frames: the port needs setFrameRate(60) or pacing).
  Page size 4 KB on this phone (16 KB alignment kept anyway). No input events were logged (nobody touched it).
- Gotcha: build.ninja is per target; a stale Switch build.ninja failed without DEVKITPRO set. Unproven: the
  probe's audio was not listened to; controllers untested.

## 2026-11-03 (evening): Android milestone 2: the game boots on the phone; memory identical to PC
- `configure.py --target android` builds the game: build/gen + runtime with NDK clang (aarch64/x86_64-linux-android30,
  PIC), platform = `platform/host` (POSIX branch) + new `platform/android/render_android.c` (no picture; frames recycled,
  SDL events pumped). minSdk raised to 30: bionic has C11 `<threads.h>` from API 30. `main.c` uses SDL_main on
  Android and turns KEY=VALUE launch words into environment variables; `platform_host.c` chdirs to the app folder,
  rotates the log, reads env.txt; `rt_log` also writes logcat (tag `sonicriders`). Built first time, 168 MB with debug
  info; the APK step now strips (26 MB). [confirmed]
- S23 Ultra, Android 17: boots from the pushed ISO, 59.9 fps, AX sound through SDL (OpenSL ES, 32 kHz). [confirmed log;
  sound not listened to]
- Det compare phone vs PC first differed at step 2 (settings: PC widescreen + Chaos Emerald "as in the save" vs phone
  defaults; block 26 = REL data the options patch), then at step 1494: the phone got perftest's START a frame earlier.
  Cause: `perftest_pad` timed START by `vi_field_count()`, which is not deterministic during the intro even with
  SR_DETCLOCK (SR_INPUT uses `ppc_detclock_steps()`). Fixed to use the step count under SR_DETCLOCK. After it: phone =
  PC on all 4,000 steps (28 MB each), x86_64 emulator = PC on 1,500. [confirmed] Not compared directly with a Switch run.
- Gotchas: stripped .so files kept a temp name in the APK at first (`libc++_shared.so not found`); a folder created by
  `adb shell mkdir` before the app's first start is unreadable for the app (emulator): the setup starts the app once.

## 2026-11-03 (night): Android milestone 3 started: Vulkan renderer and touch controls; first picture on the phone
- Research for the touch layout: Sonic Riders' controls (sonicriders.org glossary): A jump (hold = charge), B/X boost,
  L/R drift, L+R tornado, START pause; virtual-joystick guidance (floating stick, dead zone, activation zone). Layout
  mockup shown to the user (scratchpad); same numbers in touch_android.c.
- `render_vk.c` (~1,200 lines) and `touch_android.c` built first time; `render_android.c` (M2 stand-in) removed.
  S23 Ultra, Android 17: Adreno 740 Vulkan 1.3.128, dual-source + push descriptors used; intro FMV on screen at
  59.9 fps with the FPS counter and touch controls in the self-capture. [confirmed, one run] First pipelines took
  150-300 ms each (hitches at boot). Not yet seen: title screen, races, EFB copies, touch by hand, resume.
- The user reported the screen going to sleep after 10 minutes: M2 never created a window; now SDL's window +
  SDL_DisableScreenSaver (keep screen on). [likely; not timed]
- Switch dksh all `per_warp_scratch 0` after the gx.vert change (guarded by DEPTH_ZERO_ONE). Host + Switch build.
- Touch icons from the user (zip of 1024 px PNGs: joystick, A, B, R, L+R): cropped to content, slivers of neighbouring
  buttons removed (drift, tornado), the joystick split into a base (knob well filled dark) and a knob, 384 px, in
  `android/touch_icons/` (the user's own art, not from the game; gitignored, since the guard refuses binary files: without
  them the build uses the drawn icons). `tools/embed_icons.py` (stdlib PNG decoder) makes a
  premultiplied atlas header; render_vk.c draws them at 55% (pressed 85%, 7% smaller). Phone self-capture: shown
  over the intro at 60 fps. [confirmed] PAUSE / MENU keep the drawn icons (no pictures given).


## 2026-11-03 (late night): Android M3: picture upside down fixed, pipeline prewarm, resume, GPU-bound races
- **Picture compare** (S23 Ultra, Android 17): `SR_PERFTEST=1 SR_DETCLOCK=1 SR_FRAMERATE=0 SR_SHOTSTEP=<16 game
  frames>` on the PC and the phone (same settings.ini; new `SR_TOUCH=0` hides the touch controls), cropped to the
  16:9 picture and diffed. Every game frame was **upside down** on the phone (overlay right way up), races had
  missing models and smeared blur. Cause: the Vulkan prelude used deko3d's `CLIP_Y -1`; Vulkan's clip -y is row 0,
  as in GL's flipped storage, so it needs +1. That also inverted face culling and made EFB copies read the wrong
  rows. After the fix: mean difference 0.2-3.4 levels on all 16 frames (edges; the title rings differ by one frame
  of animation because the phone saved frame 3300 and the PC 3301). [confirmed]
- **Pipeline hitches**: pipeline keys recorded in `vk_pipeline_keys.bin` (app folder), prebuilt on a helper thread
  at boot and handed to the render thread (`pipe_drain`). Second run: 35 of 35 built in 68 ms, none during play.
  [confirmed] The remaining race-start hitches (~90 ms) are not pipelines: the `[hitch]` line now breaks the render
  thread's frame down (pipelines, GPU wait, images, copy-in, recording, acquire, submit, present) and shows
  vkQueuePresentKHR blocking, i.e. the GPU.
- **GPU-bound 1P races** (new `[vk] GPU x ms/frame` from timestamps): 18-24 ms per 1P race frame at 1x, CPU side ~4 ms;
  1P demo 55 fps, 2P demo 58 fps; game thread ~5 ms/step. EFB render area limited to the used part (A/B with
  `SR_VK_FULLAREA=1`: no change). `SR_VK_TINYSCISSOR=1` (1-pixel scissor): 9-10 ms remain, so per-draw/vertex cost
  (~4.5 KB of uniform blocks per state) is about half. [confirmed numbers, 1 run each] Next: specialized shaders.
- **Background/resume**: HOME for 15 s, then back: the game stays paused (render thread blocked by SDL), swapchain
  remade, frame pacer restarts, 59.9 fps at once, intro continues. [confirmed, 1 run]
- User test by hand: touch works; Xbox Series X pad over Bluetooth works (touch controls hide). Feedback list in
  STATE.md (stick rings off-centre in the knob picture, START icon, overlay touch unreliable and Close not closing,
  hide Fullscreen, controls text, app icon, analog trigger click threshold). Not done yet.
- Scope decision (user): local split screen up to 2 players with pads; 3-4 players only online.
- Touch pictures committed with the user's OK: guard exception `OWN_ART` (exact paths + SHA-1 + PNG signature).
- Later: the user supplied a new joystick cap picture (rings centred). Its "transparency" was a baked-in checkerboard:
  cut out on a circle fitted to 360 edge points of its dark rim (sd 2.8 px), 1.5 px soft edge, 256 px; replaces
  `stick_knob.png` (OWN_ART hash updated), same opacity and stick logic. Installed on the phone. [likely; not seen on
  the phone by the user yet]

## 2026-11-03 (night): Android touch/controller feedback round 2, app icon, trigger calibration
- **Menu "Close" did not close** (user): a touch on Close runs `close_menu()` inside the ImGui frame, so the picture
  made in that frame still showed the menu, and with the FPS counter on (steady 60.0) nothing drew it again. Fix:
  `overlay_image` compares the open state before the frame and redraws once more. Found by reading the code;
  checked on the phone with `adb shell input tap`: Close hides the menu, touch buttons back. [confirmed]
- **Taps on the menu**: a burst of 5 synthetic taps on different tabs landed every one (the last tab ended
  selected); tabs and Close respond. The user's "taps sometimes ignored" was probably the stale picture above
  (tapping a menu that was already closed). [likely; real-finger test by the user pending]
- Android menu: no Fullscreen option; Controls tab explains the touch controls and any Bluetooth/USB pad; hint row
  "Tap to choose. Controller: LB / RB tabs, A choose, B back". [confirmed on the phone]
- **Stick**: home position moved north-east (0.36 H from the left, 0.31 H above the bottom); dead zone now applied in
  `touch_apply` by game mode: 12% in races, 35% with a squared curve in menus (cursor moves from ~64% of the throw
  instead of ~30%). **Game mode variable `8021B8C0`**: found by phone RAM dumps (det route: intro, title, title
  again vs 1P and 2P demo races; 75 candidate words), then checked on a PC menu route: 1 through intro, title and
  Player Entry, 8 from the Free Race intro (step 6825; screenshots). Only mode 1 counts as menus, so unknown modes
  keep the race feel. [confirmed for those screens; story/other modes unchecked]
- **PAUSE**: a circle with START in block letters (vk_touch.frag), radius 0.05 → 0.065 H. Looked at on the phone:
  centred and readable. [confirmed]
- **App icon**: `tools/android_icon.py` (adaptive icon, made at build time) from the save icon in `_Main.rel`, or
  `configure.py --icon` / setup `-Icon` (.ico with PNG or 32-bit BMP images, or .png; also the PC .exe icon). The
  user's `SonicRidersIcon.ico` is the game's box art (SEGA's artwork), so it is a local option only, never
  committed. `aapt2 dump badging` shows `res/mipmap-anydpi-v26/ic_launcher.xml`. [confirmed in the APK; launcher look
  not checked]
- **Triggers**: input_sdl.c learns each pad's deepest trigger press (start 26000), scales the analog value to it and
  clicks the digital L/R from 85% of it; each new deepest press is logged. Replaces the fixed 29000 click that an Xbox
  Series X pad on Android did not always reach. [uncertain: needs the user's pad; read the `[input] ... trigger` lines]
- Builds: Android, Switch and Windows (mingw) all build.
- Session hygiene (user asked how to spend fewer tokens): docs/MAP.md (code map), REFERENCE "Quick commands",
  CLAUDE.md "Token budget", old test outputs moved to `build/old/`.

## 2026-11-03 (late): user test accepted; docs split into notes; working rules from LostOdysseyRecomp
- **User test on the S23 Ultra: accepted** — stick feel, menu taps and Close, START button, triggers in races. Log:
  RT pressed to 32767 (clicked), LT only light presses (5349, 8328: analog, no click), as designed. The APK on the
  phone now uses the user's own icon (local `--icon`, box art, not committed). Right stick → C-stick checked in code:
  stick8 (round 25% dead zone, full ±100, up = higher) into `GcPad.c_x/c_y`, sent unchanged by si.c's poll
  response. [confirmed in code; no in-game C-stick screen checked]
- Docs (ideas from LostOdysseyRecomp's AI_WORKFLOW.md and docs/notes): REFERENCE.md split into `docs/notes/`
  (build-and-test, android, debug-variables, game-addresses, architecture) with REFERENCE.md as an index of
  Reference / Historical notes; recon and the superseded decomp plan to `docs/archive/recon-2026-10.md`; HISTORY
  entries 2026-10-16..25 to `docs/archive/history-2026-10-16_to_25.md` (verbatim). CLAUDE.md: four status words
  (implemented / tested / accepted / released), "partial test ≠ full validation", "not reproduced ≠ fixed", and
  revert-first for release regressions. Local hooks (`.claude/settings.local.json`, not tracked): a session-start
  reminder and an end-of-session checklist before `git commit`.

## 2026-11-03 (late night, user away): Android specialized fragment shaders
- What: the Switch's ahead-of-time specialized fragment shaders, ported to Vulkan: `gen_shaders.py --vk` (glslc,
  same keys from build/shaderkeys/), `spec_bits()` in render_vk.c puts the shader in the pipeline key, misses go to
  `shader_keys.txt` in the app folder, `SR_NOSPEC=1` turns it off.
- Tested on the S23 Ultra (Android 17), same APK, `SR_PERFTEST=1 SR_DETCLOCK=1 SR_FRAMERATE=0 SR_TOUCH=0`, off vs on:
  1P demo 55.13 → 59.29 fps, 2P demo 58.37 → 59.81 fps; GPU per frame title 9.3-11.4 → 1.6-1.8 ms, 1P race
  13.7-18.2 → 11.1-14.1 ms, 2P race 13.0-17.7 → 12.0-13.9 ms. All demo states hit (32 keys). Captures at game frames
  2900, 5300, 10500: identical except the FPS counter's digits (<0.015% of pixels, all in its box). [confirmed, 1
  run each; not accepted by the user yet]
- Not done: the vertex format (160 bytes per vertex) is the next GPU lead; first race frame after a new build
  still builds its new pipelines (178 ms hitch once, then prewarmed).
- Hook fix: the commit checklist hook fired on every Bash call (`if` filter ignored); it now greps the command.
- Later the same night: a packed Vulkan vertex (116 vs 160 bytes) A/B'd on the phone: no change in fps or GPU time,
  identical pictures; reverted (vertex fetch is not the cost). USCALED8 is not a vertex format on Adreno. [confirmed]
- PS4/PS5 research for the user (docs/notes/ps4-ps5-research.md): public PS5 jailbreak to 13.60 (Relapse +
  etaHEN), 14.x not public; an open PS5 Vulkan driver (Mesa RADV port, PS5_Vulkan) is in use by RetroArch,
  Dolphin and recomp ports, so a PS5 build could reuse render_vk.c. PS4 public to 13.52.
- PS Vita research for the user (docs/notes/vita-research.md, research only, no code): HENlo runs HENkaku on
  3.65-3.74, Ensō needs 3.65. Guest memory fits easily; the CPU is the blocker: estimated 50-80 ms per game step
  at 444-500 MHz vs 33 ms needed for the game's 30 fps mode [uncertain, no hardware measurement]. Closest
  precedent, Xita (Xbox → Vita static recomp), reached 12-20 fps and closed. Remote control: vitacompanion (FTP
  1337, commands 1338: launch/press/quit), screen via udcd-uvc over USB. Suggested probe: headless core +
  GXM 7,000-draw microbench on the user's PCH-2000.
- 2026-11-04: **v0.4.0 released with the Android port.** Before it, the user's phone crashed while screen
  recording (VK_ERROR_DEVICE_LOST after ~330 ms frames at the first race frame). Reproduced 4/4 (perf test +
  `adb shell screenrecord`) and once without recording on a warm phone; ruled out EFB rectangle bounds, long
  submissions (split at 1,500 draws), robustBufferAccess, the acquire wait stage, and pixel cost (1-pixel scissor
  still crashes). Cause unknown [uncertain]; shipped as a known issue by the user's decision (docs/notes/android.md).
  PC and Switch built from the merged tree; 390 Switch dksh with per_warp_scratch 0; no hardware run of the merge.
- 2026-11-04: **Vita probe V1a on hardware** (branch `vita`). Fixed in order: pthreads only on its own threads,
  `_Thread_local` shared across threads (GCC emutls single-thread mode until `pthread_cancel` is linked: guest
  handoffs deadlocked), then the perf test ran: CPU 500 (plugin override of 444), game thread 52.1 ms/step 1P /
  43.0 ms 2P, GP thread 42-51 ms [confirmed]. Remote: vitacompanion 1.08 installed, udcd_uvc enabled (USB camera
  "PSVita" read with ffmpeg). docs/notes/vita-research.md.
- 2026-11-04 (d): **Clean-install release test in Windows Sandbox** (`main`). New `tools/sandbox_test.py`: the
  release zip, marked as downloaded (ZoneId=3) and extracted by the Explorer shell, installed by its .bat with
  the unattended flags in a fresh sandbox (8 GB, disc folder read-only, results folder the only writable one).
  Found and fixed, each by a cold rerun: dtk.exe exits 0xC0000135 on a clean Windows (VCRUNTIME140.dll; now
  copied from the embeddable Python); Android-only setups lacked `--imgui`; **every Android build from a release
  zip (v0.4.0 included) failed at the APK step** (VERSION "v0.4.0 (commit)" unquoted in `--version-name`); a
  reset connection in the NDK download ended the setup (5 tries with resume now; the retry path itself has not
  been hit in a test yet [uncertain]); `adb devices` left an adb server running. The .bat now returns setup's
  exit code (`SR_NOPAUSE=1` skips the pause). Final cold runs on commit 174b9811 (zips v0.4.1-rc5): PC and
  Android PASS, each in `C:\SonicRiders` and `C:\Sonic Riders Test` [confirmed]. Not tested: playing (no GPU in
  the sandbox), the Switch zip. Procedure: docs/notes/build-and-test.md.
- 2026-11-04 (f): **v0.4.1 released** (tag c263ebe; PC, Switch and Android zips): the setup fixes of (e). Cold
  sandbox PC + Android PASS on the tagged commit (both folders) before publishing [confirmed]. The v0.4.0 GitHub
  release was deleted at the user's request (its Android download was broken); the v0.4.0 tag remains.

## 2026-11-05: Android setup skipped the install silently (#14)

- Issue #14: the setup built the APK, then step 7 skipped the install without a word because `adb devices` showed
  no authorized phone. Found from the reporter's setup.log. Fix (ceb9502): log why (no phone / not authorized /
  several) and what to do. The v0.4.1 Android zip was rebuilt from ceb9502 and replaced in place; tag unchanged.
  Tested from the refreshed zip: cold Windows Sandbox Android PASS (both folders, 2 min each) [confirmed]; on this PC
  with no device, step 7 prints "no Android phone found" [confirmed]; with the x86_64 emulator (srtest) attached it
  installs (versionName v0.4.1 (ceb95020)) [confirmed]. Not tested: the "not authorized" and "several devices"
  messages (simulated only), a real phone. The arm64 APK was not running 20 s after start on the emulator (arm64
  under translation; earlier emulator runs used an x86_64 build) [uncertain, not the setup].
- #13 closed (reporter: installs, 60 fps on Snapdragon 7 Gen 3).

## 2026-11-05 (b): Android crash reports a player can send without a PC

- Why: a Reddit user wrote that the Android build "keeps crashing on launch", and players had no way to send a log:
  `sonicriders.log` sits in `Android/data/net.srnative.sonicriders/files`, which phone file managers cannot open
  since Android 11, and the POSIX build had no crash handler (only Windows logged `[crash]`).
- Change: native `crash_signal` in platform_host.c (crash.txt + logcat + log, backtrace as lib+offset; chains to
  debuggerd), and `CrashReport.java`: on the next launch, a run that ended badly (Android's exit record or a
  crash.txt) becomes `Download/SonicRiders/sonicriders-crash-<time>.txt` plus a dialog with Share report. Details:
  docs/notes/android.md "Crash reports".
- Tested on the x86_64 emulator (Android 15) [confirmed]: missing disc (`missing.iso` -> rt_fatal -> SIGABRT) gives
  crash.txt with a stack into libmain.so/SDL, and on relaunch a dialog "cannot open the disc image missing.iso", the
  file in Download/SonicRiders, and a working share sheet; `kill -SEGV` while playing gives a report and debuggerd
  still writes its tombstone; a force-stop and a reinstall give no dialog. Not tested: a real phone (arm64 build
  compiled only), a real fault inside recompiled code, a crash on a thread other than the one with the alternate
  signal stack (stack overflow there gets no crash.txt; the exit record still triggers the report) [uncertain].
  Cosmetic: the share sheet shows the MediaStore id instead of the file name [uncertain whether receiving apps do].
- The redditor's cause is unknown; a missing or failed disc push shows exactly as a "crash on launch" [likely].
- 2026-11-05 (c): **v0.4.2 released** (tag b790304; PC, Switch, Android zips). Before publishing: cold Windows
  Sandbox PC and Android PASS, both folders [confirmed]; on the S23 Ultra (arm64, Android 17) the `missing.iso`
  launch logged `[crash] SIGABRT` with a stack through the signal frame into `SDL_main`, the next launch showed the
  dialog and saved the report to Download/SonicRiders, Close kept the game running, and a force-stop + relaunch
  showed no dialog [confirmed]. Not tested on a phone: a fault in recompiled code, Share report to a real app.
- 2026-11-05 (d): the user screen-recorded on the S23 Ultra with v0.4.2 installed (build b790304) and the game did
  not crash. Not reproduced is not fixed: v0.4.2 changed nothing in rendering, and the earlier 4-of-4 repro used
  `SR_PERFTEST=1` + `adb shell screenrecord` [uncertain: recorder, length and scene not known yet]. Known-issue
  note kept; a new crash would now leave a report (Download/SonicRiders).
- 2026-11-05 (e): minutes later the user's v0.4.2 session crashed loading a race (screen recording). The new crash
  report worked end to end on a real crash [confirmed]: next launch wrote the report with
  `FATAL: [vk] vkQueueSubmit ... failed: -4` (device lost), SIGABRT stack through rt_fatal. Same pattern as the
  open issue: 4 frames of 299-339 ms each, all in `present` (297 ms), 2,769 draws/frame (fewer than the ~6,000 seen
  before), then the loss; the window before showed GPU 8.3 ms/frame avg, max 25 ms. So (d) was luck, not a fix.

## 2026-11-06: Android device-loss recovery; screen-recording loss bisected, not fixed

- Issues: no replies on #14 (music stops) or #12, no crash report from the Reddit "crashes on launch" user yet.
- **Recovery** (render_vk.c `device_recover`, `device_setup` / `device_teardown`): VK_ERROR_DEVICE_LOST at submit,
  present, fence waits or acquire drops the frame; the next frame destroys everything on the VkDevice, makes it
  again, reloads the pipeline cache file, waits for this run's pipelines (prewarm) and re-uploads textures; a 4th
  loss within 10 s is fatal as before. `SR_VK_FAKELOST=600,4500` on the S23 Ultra: recovered in 364 / 359 ms,
  captures 2 frames after each are right (intro, 1P race with HUD and blur) [confirmed]. Real losses with
  `screenrecord`: each rebuild worked, but the new device was lost again ~10 frames later, 3 times, then fatal: the
  repro does **not** survive (4 of 4 runs, plus every bisect run below) [confirmed]. Normal perf test, alternating
  A/B (base / new / base / new): 1P 59.74 / 59.43 / 59.68 / 59.65 fps, 2P 59.94 all, race GPU 12.27 / 12.46 /
  12.43 / 12.41 ms: no change [confirmed, S23 Ultra].
- **Bisect with recording** (~90 s per run, details: docs/notes/android.md "Open: device lost"): no draws or ≤1,000
  draws per frame survive; the slow frames' GPU time is ~12 ms while present blocks ~330 ms. Still lost with: 3
  swapchain images, pre-rotation, queue priority low/high, 300-draw parts with idle GPU between, 30 fps, no
  dual-source blending, no push descriptors, fixed UBO offsets, ubershader only, ≤20 pipeline switches, 3 indices
  per draw, dynamic depth write, D32/D32S8 depth, an LRZ-off draw. Survived: one pipeline per pass, depth writes
  forced on; but the forced picture is badly wrong and much lighter on the GPU, so the depth-state lead is
  [uncertain]. A crash found on the way (recording into a command buffer after a lost part submit) was in the
  split experiment only, which was removed.
- Kept as test switches: `SR_VK_FAKELOST`, `SR_VK_DRAWS`, `SR_VK_ONEPIPE`, `SR_VK_FORCE`, `SR_VK_SWAPIMAGES`,
  `SR_VK_PRETRANSFORM` (pre-rotation: renders right per the log, picture not checked on the panel); the rest is in
  `build/old/android-devicelost-experiments-2026-11-06.patch` (local). `[hitch]` lines now give the frame's
  pipeline switches and the last GPU frame time; the 300-frame report gives switches per frame.
- Vita paused by the user (GXM renderer chosen over the fault-handler idea: ~50,000 GX pipe stores per race frame
  go through the device range, so faults there are not an option without recompiler work). iOS: the Mac is not
  updated yet.

## 2026-11-06 (afternoon): Linux / Steam Deck build; issue #14 log; ultrawide/FOV and boost-effect plans

- **#14 (Android music stops):** the reporter pasted a log (Adreno 810 phone, v0.4.2) that ends ~20 s in, before the
  music stops; it shows 70-150 ms game-code hitches every few seconds. Lead: the CRI music stream thread (prio 31)
  starved by those frames [uncertain]. Replied asking for the attached file, the phone model and a 30 fps test.
- **Linux build** (gbatemp request: the .bat can't run on a Deck): `configure.py --toolchain linux`; the runtime
  compiled as is (POSIX paths from Switch/Android). Link: GL 1.1 calls need libGL; SDL2 and GL are linked by the
  full path of the runtime `.so.0`/`.so.1` because SteamOS has no -dev links. Found by running it in WSL:
  (1) exit hung: glibc's `pthread_cond_destroy` (C++ static destructor of menu.cpp's condition variable) waits
  for the helper thread blocked on it; now never destroyed [confirmed]. (2) Then a crash at exit: `SR_PERFTEST`
  called `exit()` on the game thread while the render thread presented, and Mesa's driver was unloaded under it;
  the perf test now quits through the render thread like the menu's Quit (Windows: 59.94 fps both demos, exits
  normally) [confirmed].
- **Setup** `SonicRidersSetup.sh` -> `tools/setup_linux.py` (system python3; dtk, ninja, imgui, SDL2 headers, zig
  0.17.1 pinned in toolchain.json "linux"). A bubblewrap "Deck-like" sandbox (no compiler, linker, headers or
  sdl2-config) found three more bugs: sdl2-config's presence taken for headers; zig's linker rejects `-Map`; the
  wrapper waited for Enter when it had no terminal to open. All fixed; the zip-based run then passed end to end
  [confirmed in WSL]. Not tested: a real Deck, Game Mode, Steam Input, AMD/Mesa drivers [uncertain until a user
  report]. WSL GL is llvmpipe or d3d12-on-iGPU, so no fps conclusions.
- **Ideas:** Discussions #15 (ultrawide + FOV) answered with a plan, docs/notes/ultrawide-fov.md (the FOV input is
  r4 of `fn_800645DC`); the user's 3440x1440 monitor is the target. Boost camera effect (NFS-style, levels 2/3):
  docs/notes/boost-effect.md, Discussions #16. User's order: ultrawide/FOV next, then online play.


## 2026-11-06 (evening): ultrawide without stretching + FOV setting; Android setup counted one phone twice

- **GitHub:** no new replies on #14, #12, Discussions #15/#16; the Linux pre-release has no Deck report yet.
  gbatemp first post drafted for the user (kept locally).
- **Ultrawide (#15):** new "Fit window" mode (window/screen aspect, 4:3..32:9); the 16:9 and fit modes draw a
  wider EFB (640 x k) instead of stretching: k and flags travel in unused XF registers 0x7D-0x7F of each state;
  backends scale viewport/scissor/copies/present X by k; gx.vert keeps orthographic draws at 4:3 in the middle
  except vertices on a screen edge (full-width quads still fill). Option "stretch HUD". Captures on the det route
  at 16:9, 21:9 (3440x1440) and 32:9: race, 2P split and title right, 59.8+ fps [confirmed on PC]; Switch in Eden
  16:9 same as PC [confirmed in Eden]; Android emulator 20:9 fills the screen [confirmed in the emulator]. Not
  on hardware, not seen by the user. Known: seams at the title bands' corners; speed-blur outline at 32:9/FOV 130.
- **FOV:** the plan's hook was wrong: `fn_800645DC` tail-calls a translation-matrix builder (`fn_8004C9E0`), so
  its r4 is not a field of view [confirmed by reading the generated code]. FOV is done in gx.vert instead (scale
  the projection's vertical angle, 80-130 %), render-only.
- **Determinism:** RAM hashes over 6000 det-route steps: FOV and HUD stretch change nothing [confirmed]; the camera
  aspect changes game RAM from the race start (even 1.3333 vs 1.3344) [confirmed], so online sessions need one
  camera aspect for all players (open, ONLINE.md). 16:9 now rounded like the fit mode: 16:9 = fit at 16:9 [confirmed].
- **Android setup:** the user's own v0.4.2 build skipped the install: "3 devices" = the emulator I had running
  plus the S23 Ultra over USB and over wireless debugging. setup.ps1 now counts devices by `ro.serialno`, prefers
  USB and sets ANDROID_SERIAL [likely: the dedupe tested by hand on the two entries, not a full setup run].

## 2026-11-06 (evening): v0.5.0: edge HUD, wide-mode fixes, mouse menu, online groundwork
- **User report** (PC 3440x1440, Fit window; shots in build/old/user-shots-2026-11-06): gradient bottom left in
  menus, a faint copy of the character beside it in every non-4:3 mode; wants the HUD at the edges in Fit window,
  16:9 with 4:3-middle HUD (+ stretch option), no "16:9 stretched" mode.
- **Mirage cause [confirmed]**: the per-vertex 2D rule (vertices at the edge kept, the rest /k) squeezed the inner
  vertices of the 18-vertex speed-blur copy quad (512x256 EFB copy of the last frame) against its texture; the
  gradient was an edge piece of a menu band stretched the same way. **Fix**: per-draw anchors (gx_render.c
  `wide_anchor`, normal's w, gx.vert): full width / bands stretched, others middle, and in Fit window during races
  (GAME_TOP_MODE 8) left/right of a 0.3 dead zone to the edges. Edges in menus were tried and split text lines and
  icon rows, so menus stay in the middle. Text glyphs chained into bands until bands needed 0.1-wide pieces with
  similar heights. [confirmed by captures: PC det route 16:9/21:9/32:9 + 2P, menu route + race pause 21:9, Eden,
  Android emulator 20:9]. Not checked: results screen, story, 3-4P; phones: edge HUD under the touch buttons.
- "16:9 stretched" removed (saved value 2 -> 16:9).
- **PC mouse in the F1 menu** (render_gl.c -> overlay_mouse_*): click, tabs, slider click and drag [confirmed with
  posted WM_ mouse messages]; wheel coded, nothing scrolls at 720p to check; fullscreen cursor untested.
- **Online** (not tested between machines): host camera aspect in the welcome (protocol 3) and recordings;
  backstops off during sessions; x86 NaN creation -> +NaN in the recompiled float ops (ppc_nan2/3). PC perf test
  after the NaN change: 1P 58.6 fps / 2P 59.7, game 1.6 ms/step [likely unchanged; the PC was busy].
- Found: Git Bash `python` and `py` are devkitPro's msys python, which writes forward-slash program paths into
  build.ninja (CreateProcess failed); configure with the Windows Python312 path.
- Steam Deck: first user report on gbatemp (works; fullscreen borderless shows 4:3 borders, "16:9 stretch" did
  not work (now removed), Chaos Emerald gear option did not work) -> next session.
- Released v0.5.0 (PC, Android, Switch, Linux zips) after the cold sandbox tests.

## 2026-11-07: ultrawide fixes after the user pulled v0.5.0; Switch HOME icon in the setup; Deck items

- **User report (3440x1440, Fit window, v0.5.0 PC):** screenshots of Extras/Options menus and Theater movies.
  Every shot also showed the F1 menu: "Save screenshot" captured the frame that drew the menu. Fixed on PC
  (pixels read before the overlay), Switch and Android (request taken before the overlay is drawn, so the next
  frame, menu closed, is saved) [likely; PC path built, button not clicked in a test].
- **Torn text** ("That's reall ... dar signal!", "Check Ex ... obtained."): the race edge rule was gated on
  `GAME_TOP_MODE` 8, which stays on in menus after story mode (user log: mode 8 from frame 22668 to the end).
  Found `GAME_MENU_FLAG` 0x8021C1AC by RAM dumps (det route: Free Race, pause, quit dialog vs title, Player
  Entry, Normal Race, Extras); edges now need mode 8 + flag 0 (8 menu frames to leave; the flag blips in
  races). Also the race's quit dialog was torn (each letter is its own draw): consecutive letters on one row
  now share the line's anchor. Menu bars: thin offset pieces now join bands (35 % shared height for pieces
  < 0.15 high; 35 % for all pieces stretched the round backgrounds, tried and reverted). Checked at 21:9 on PC:
  race HUD, pause, quit dialog, Mode Select, Normal Race, Extras, Gear Gallery [confirmed]. Not seen: story
  cutscenes / Theater story movies (the user's subtitle case), Options, Records, results screen [uncertain].
- **HOME-screen icon (user: same priority as ultrawide):** tools/nsp/make_forwarder.py (own code, stdlib):
  pinned nx-hbloader 82b9512 + hacBrewPack 745b16e downloaded and tree-hashed, patched, loader built with the
  setup's devkitA64/switch-tools, hacBrewPack with llvm-mingw clang, NACP + 256x256 JPEG icon (own JPEG
  writer; game save icon by default), packed with the user's prod.keys. Built here in 15 s; packed NPDM
  byte-identical to the hw16 forwarder that worked on hardware [likely; the new NSP not installed yet].
  setup.ps1 asks after "What to build" (keys from %USERPROFILE%\.switch or a file dialog, optional picture),
  copies to <sd>\NSP\, prints DBI/Goldleaf steps. Setup prompts not run end to end [uncertain].
  build_nsp.ps1 and the .patch file removed (superseded).
- Chaos Emerald option default off (was on), new key `chaos_emerald_shop`, clearer text. Steam Deck: gamescope
  / SteamDeck=1 -> window at display size, first run fullscreen + Fit [untested on a Deck]. "Save a bug
  report" zip (PC/Linux): tested on Windows with SR_BUGREPORT=1 (zip valid, 5 files) [Linux not compiled].
- Research: no public look-back camera code found; plans in docs/notes/plans-2026-11.md (look-back, boost
  toggle, launcher, building on Linux/Android, icons per device). Online play: not worked on this session.

## 2026-11-07 (b): ultrawide checks finished; movies 4:3; results screen; setup icon run; Linux build

- **Reports:** #14 (Android music stops): the reporter attached a log (Nothing Phone 3a, Adreno 810, v0.4.2,
  30 fps mode). 489 hitches; most are 50-150 ms of "game code" even on the title screen (71 draws, normal
  ~2.5 ms), so the game thread is stalled, not slow; audio "mixes started late" up to 20 ms. Lead: Android does
  no thread placement or performance hints at all (platform_host.c `rt_thread_*` are empty there) [uncertain].
  Discussions: nothing new.
- **Ultrawide checks (PC, 21:9 Fit window, det clock, scripted routes from an empty card):** Options, Extra,
  Audio Room, Records, Theater menu, story intro movie + subtitles, a full 3-lap Free Race with results; det
  route at 21:9 / 32:9 / 16:9 widescreen, 2P split [confirmed by frames]. Two bugs found and fixed:
  - Movies were stretched to the screen width (1.75x at 21:9). A movie frame is one screen quad, 1 stage,
    640x480 RGBA8 texture that is not an EFB copy -> now 4:3 with black bars (`is_movie_draw`) [confirmed].
  - The results screen was torn to the edges (the user's v0.5.0 shot): the menu flag is 0 there. RAM search:
    `8021C1B8` and the racer array `801D746C` looked right in sparse dumps but flip during races (seen in the
    `[wide]` log); new `SR_RAMSAMPLE` (log changes of up to 4096 words per step) found `801DFC80`
    (`GAME_RACE_PHASE`): 0xE racing, 0xA from the finish line on, steady for a whole race. Edges now end at
    the finish [confirmed: whole race at the edges, results whole]. Unproven: story/Tag/Survival, 2P with one
    player finished.
- **Setup end to end with the HOME-icon question** (Switch, -NoSd, answers piped: y / y / n): see STATE.
- **Linux build** (WSL Ubuntu 24.04, clang): compiles; runs (llvmpipe); "Save a bug report" zip has its 5
  files on Linux [confirmed in WSL, not on a Deck].
- **Release prep v0.5.1:** zips (Switch/PC/Android/Linux) from 900495f; cold Windows Sandbox: PC PASS (2 + 1
  min), Android PASS (2 + 2 min) [confirmed]. Not published: waits for the user's HOME-icon hardware test.
- User tip: the stage select has race options with the lap count; set 1 lap for quick race tests.
- **v0.5.1 published** (tag v0.5.1 at 900495f, the zips' commit; 4 zips incl. Linux test). User built it from the
  Switch zip on the console: **performance regression** (struggles to hold 30 fps overclocked; resolution and
  screen mode changes don't help). Released anyway at the user's request with a "stay on v0.4.2 for Switch"
  warning; fix next session [cause unknown].
- HOME-screen icon: the user installed the NSP made by the v0.5.1 setup; it opens the game [accepted]. The
  v0.5.0-linux-test pre-release was deleted (tag kept).

## 2026-11-07 (b): Switch "regression" = release builds never had specialized shaders; v0.5.2

- **What was wrong:** the user's v0.5.1 log (built from the release zip) said `[deko3d] 0 specialized fragment
  shaders`, "specialized shaders for 0 of 33520 states": races drew everything with the ubershader, GPU 30-45 ms
  per frame, 20-35 fps; game work only 3-7 ms. The keys came only from `build/shaderkeys/` on the dev PC
  (game-derived, never shipped) and the setup never made any, so **every release build** (v0.4.x and v0.5.x,
  Switch and Android) lacked them [confirmed: the v0.4.2 zip build's gen header says `GX_SPEC_COUNT 0`; a v0.4.2
  build on a second console logged 0 shaders and GPU 32-38 ms; the #14 Android v0.4.2 log says `[vk] 0`]. The
  "v0.4.2 ran at ~60" memory was dev builds. v0.4.2..v0.5.1 per-frame changes (wide_anchor on 2D draws, wide
  EFB 1708 columns in 16:9) are small next to this; gx.frag unchanged.
- **Key coverage:** dev set 32 keys (normalized); the user's v0.5.1 session 30 keys, all in the set; a 6-min
  unattended PC attract run records 21 of them (SR_HEADLESS records none: no rendering).
- **Fix (user's decision):** the key list is tracked at `config/GXEE8P/shader_keys.txt` (CLAUDE.md rule 1 has
  this one exception); configure.py reads it plus local `build/shaderkeys/*.txt`.
- **Tested:** Switch build with only the shipped list: 32 shaders. OLED **handheld** (CPU 1785 / GPU 614 / MEM
  2133) `SR_PERFTEST`, Fit window: demo 1P 59.9 fps, demo 2P 59.9 fps, game 8.4/8.8 ms mean, 0 steps over 16 ms,
  33536 of 33536 states specialized, GPU <= 9.4 ms/frame [confirmed] (`perf/results/2026-11-07-switch-handheld.json`).
- The user's clocks, corrected: handheld 1785 / 614 / 2133, docked 1785 / 1152 / 2133 (older entries said 1152
  for all).
- Side work: fast build + forwarders installed on two more consoles' SD cards (user's request); a .gci save
  imported into a card image (loads on PC). Found: the Switch "Save screenshot" still captured the settings
  menu in v0.5.1 (shot_7748) [uncertain: maybe pressed with the menu staying open].
- **v0.5.2 published** (tag at 8db1dc6; 4 zips; cold sandbox PC PASS 2 min, Android PASS 2 min). At the user's
  request every older release was delisted (tags kept): v0.5.2 is the only release. Clean v0.5.2 build
  (v0.5.2-1-ge471b4e9) on the OLED; S23 updated with the APK (cover-art icon like the OLED's).
- **Track Select torn in Fit window** (user report, OLED screenshot via switch-remote): the striped panel behind the
  course row touched the arrow panels (x to +-1.17) and the outer course pictures, so the band rule stretched them
  and pulled the pictures out of the cyan frame. A first try (no band when it runs past +-1.02) broke the Normal
  Race / Mode Select bars (they overshoot too). Fix: pieces 0.15+ high join a band only within 10% of the same
  height. Tested: PC 16:9 and 21:9 (Mode Select, Normal Race, Track Select, Player Entry bars to the edges; race
  HUD identical to the old rule at 21:9) and OLED Fit window (mode not checked): Track Select whole, also scrolled to Babylon
  Garden [confirmed]. Not released yet (in "Unreleased"); other menus with mixed-height pieces unchecked.
- **v0.5.3 published** (tag at ef97b2d; 4 zips; cold sandbox PC PASS 3 min, Android PASS): the Track Select fix.
  Like v0.5.2 before it, it is the only listed release (v0.5.2 delisted, tag kept; the user's rule: only the
  latest release is listed). OLED and S23 (cover-art icon) run v0.5.3.

## 2026-11-07 (afternoon): #14 thread placement + ADPF merged; Switch screenshot fix; online: PC = Switch on a 2P race
- **#14 Android thread placement** (branch `android-thread-placement`, rebased and fast-forwarded into main): S23
  Ultra (Android 17) `SR_PERFTEST`, one run each: placement 1P 59.96 speed / 59.06 fps, 2P 59.95 / 59.19;
  `SR_NOPIN=1` 59.95 / 58.51, 59.94 / 59.57; 0 of 1800 steps over 16.7 ms in all. `[android] 8 cores, fast ones:
  3-7`, no affinity/priority failures. No worse [confirmed on the S23]; whether it helps the reporter's Adreno 810
  phone is unproven (the S23 never stalled).
- **ADPF performance hint** (`platform_game_step_work`, platform_host.c, fed from hle_frame_end): the game thread
  reports its work per step, target 16.7 ms; APerformanceHint looked up with dlsym (Android 13+). S23, two A/B
  pairs: no difference (59.9 speed, 0 slow steps; a 51+60 ms render-thread hitch at the 2P race start shows up with
  and without it). On by default, `SR_NOHINT=1` off. Help on stalling phones: [uncertain].
- **Switch "Save screenshot" still showed the menu on v0.5.3** [confirmed, OLED, shot_1756, FPS counter on]. Cause:
  Switch/Android draw the overlay from the helper thread's last finished picture, still the menu's for a frame or
  two after closing; the FPS counter keeps the overlay drawn. Fix (menu.cpp): each picture records whether the menu
  was open; the request waits until the shown picture is menu-free. OLED: shot_1507 shows the game + FPS counter
  only [confirmed]. Android: same code path, not clicked on the phone.
- **Online: 2P race route rebuilt** (`tools/net_route_2p.py`; player 1's part recovered from the Switch's
  env.online.txt; player 2 joins at 3500, RIGHT x2 = Knuckles; race from ~5900; checked by PC screenshots). The race
  is 3 laps: the lap count is in Track Select's "Rule Settings" box; X/Y/Z do nothing there (next: d-pad onto it +
  A).
- **PC recording replayed on the Switch (ARM), OLED handheld 1785/614/2133:** full SR_RAMHASH equal on all 7300
  hashed frames (boot, menus, ~1400 race frames, where the NaN sign differed on 2026-10-30) [confirmed: the x86 NaN
  fix works on this route]. The replay then differed from frame 20161 (results screen): a `[det] backstop` fired
  during a 159 ms SD-card disc read. Fix (di.c/vi.c): no retrace backstop while a disc read is copied or pending.
  Proof: `SR_DET_SLOWREAD=250` PC replays: without the fix 40 backstops and differences from frame 3; with it 0
  backstops, all 23847 frames match [confirmed]. Switch replay with the fix: all 23847 frames match (that run had no slow
  read, so on the Switch it is "not reproduced"; the PC test is the proof).
- **Two PC copies over localhost** (`SR_NET=host:127.0.0.1:7777` / `join:...`, the route split per player): 35,400
  frames in lockstep, every checksum equal, full RAM equal for 8400 frames, no backstop [confirmed]. Not yet: two
  machines (PC-PC LAN, Switch-Switch, PC-Switch with this build).
- Lead from the user: Tag Force Recomp (PSP; github.com/Artknighty/tagforcerecomp-preview): plugin mods (C ABI DLL
  hooks), HD texture/movie replacement, cheats (docs/notes/plans-2026-11.md).

## 2026-11-07 (evening): Tag Force Recomp studied; Windows launcher
- User shared Tag Force Recomp (PSP recomp; release folder + github.com/Artknighty/tagforcerecomp-preview). Its
  release exe has no game code: the launcher installs from the ISO, translates and compiles (TinyCC) on the
  player's PC. Takeaways in notes/plans-2026-11.md (quick wins, launcher, runtime/game-code split, TinyCC risks).
- **Launcher implemented** (notes/launcher.md): `launcher/launcher.cpp`, Win32 + D3D11 + ImGui, look modelled on
  the user's Tag Force screenshot (sidebar, banner, ready check, quick settings, PLAY). Banner: public fan art the
  user chose (not committed: the guard refuses binaries; asked the user). Tested: builds; Play and Graphics pages
  checked by self-capture (`SR_LAUNCHER_SHOT`) [confirmed]; PLAY, the install path and settings writes not run
  yet [untested]. First idea (banner captured from the player's disc, intro frame 2101) dropped for the fan art.

## 2026-11-07 (night): launcher finished for the PC zip
- Banner "not showing" (user report): the copy in build\launcher\ looked for launcher\banner.jpg, setup\ and
  saves\ next to itself (gradient + "Not installed yet"); the root copy was fine. Fix: the root is the nearest
  parent folder with setup\setup.ps1. Both copies' captures now identical [confirmed]. Banner committed with the
  user's OK (exact-SHA-1 allow entry in the guard, JPEG accepted there).
- PLAY / Stop and the settings round trip [confirmed, PC]: `SR_LAUNCHER_TEST` set fov 111, pressed PLAY; the game
  logged fov 111; an `SR_INPUT` script with the new `M.` menu buttons opened F1, went to Gameplay and ticked the
  Chaos Emerald option (captures checked); after Stop the launcher read chaos_emerald_shop=1 back.
- Button remapping: `saves\controls.ini` read by input_sdl.c controls_load (defaults unchanged); Controls page edits
  it (key press, XInput press, or list). The launcher's file was read with 32 entries, no unknown names
  [confirmed]; real key/pad presses in a race not tested by us [uncertain].
- Icon drawn by tools/launcher_icon.py (APPICON). make_release.py --flavor pc builds the launcher from the
  committed source and adds it; README-FIRST points to it. `SR_LAUNCHER_INSTALL` + sandbox_test.py --launcher
  for the clean-install test (result below).
- **Cold Windows Sandbox install through the launcher: PASS** (dev zip at c535c77, `sandbox_test.py --launcher`):
  zip marked as downloaded (mark-of-the-web on the exe), all tools downloaded fresh, setup exit 0, game built,
  disc in baserom, launcher status "Installed. Press PLAY." [confirmed]. The game itself was not started in the
  sandbox.

## 2026-11-07, late: remapping accepted; F1 Controls tab; two sessions (PC "Mike", Mac "Mac")

- The user played a race with remapped buttons through the launcher: remapping **accepted**. They saw the F1
  menu's Controls tab still listing the default keys: it was fixed text. Now it reads the live map
  (`input_map_row()` in input_sdl.c, table in menu.cpp) [confirmed: scripted F1 run with a changed controls.ini
  (A=Space, Z=LB) shows them]. Commit 074b2a9.
- iOS work runs in a second Claude session on the Mac (branch `ios`, not merged yet). The PC session checked
  the branch: PC and Android build.ninja identical to main (except the version string); the changed C files
  compile clean with llvm-mingw and the NDK clang [confirmed]; no full link or phone run [uncertain].
- Launcher icon: a "fan-made" logo silhouette was turned down (looks extracted from an official Flash file and
  copies the logo). Fan art with characters needs the artist's permission; a request was sent to one artist
  (no reply yet). The committed banner's origin should be checked the same way.
  [2026-11-09: the icon question is closed: the icon is the user's ChatGPT art; only the banner check stays open.]
- Plan after iOS (user's order): higher internal resolution + anti-aliasing, then bloom + colour grading, then
  per-pixel character lighting.

## 2026-11-07, later (PC "Mike"): internal resolution 1x-6x and MSAA on PC

- User's plan after iOS starts early, **PC only** (user: the picture upgrades are not for Switch/Android/iOS).
  render_gl.c: the fixed `EFB_SCALE 2` became `g_sc` (setting `pc_scale`, 1-6, default 2); MSAA (setting `msaa`,
  1/2/4/8, default off) draws into multisampled renderbuffers resolved into the EFB textures before each copy
  (`efb_resolve`). A change in F1 > Display applies at the next frame; copy textures are scaled into new ones
  (`rescale_copies`) so one-time copies (menus) stay. `SR_SCALE` / `SR_MSAA` for tests.
- Tested [confirmed]: det route (`SR_PERFTEST=1 SR_DETCLOCK=1`) at 2x/off, 4x/4x MSAA, 1x/8x MSAA: EFB sizes as
  expected (wide mode too), 60 fps, same scene at step 5300, edges smoother at 4x+MSAA. Not tested [uncertain]:
  changing it live in the F1 menu (rescale path), 5x-6x, a weak GPU. Presenting a 4x+ EFB in a small window uses
  a plain linear blit (2:1 at most is clean; more could shimmer): a proper downscale filter may follow.

## 2026-11-07, night (Mac): iOS first boot on the iPhone

- Setup on the Mac from the user's disc image (copy in baserom/, Redump GXEE8P rev 0 verified; the first image in
  Downloads was Sonic Free Riders for Xbox 360). Recompiled 5208 functions; `configure.py --target ios` + ninja.
- Signing: no certificate until the user picked the Personal Team once in the generated Xcode project [confirmed].
- Link failed on EAGL/GL symbols from SDL2's UIKit code: link OpenGLES [confirmed].
- First launch: SIGTRAP in UIKit before main (found in the device's systemCrashLogs .ips): iOS 27 requires the
  UIScene life cycle. Fix: scene manifest in Info.plist + `ios_scene.m` puts SDL's window on the scene [confirmed:
  boots, intro movie, title screen after a scripted START, 60 fps, A19 Pro GPU via MoltenVK].
- Screenshots looked grey and skewed: BMP rows were not padded to 4 bytes (width 874); fixed in render_vk.c for
  every target [confirmed on iOS; Android/Switch widths were already multiples of 4].
- User: touch controls looked jagged. The swapchain was in points (874x402) stretched 3x; `SDL_WINDOW_ALLOW_HIGHDPI`
  on iOS gives 2622x1206 [confirmed by capture; later by the user].
- App icon from the user's fan-art PNG through `--icon` (local only, not committed).
- User tested the build: high-DPI touch buttons look right; an Xbox One pad works; F1 menu screen size and FPS
  toggles work [confirmed, user]. Touch did nothing in the game: `touch_apply()` in input_sdl.c was Android-only;
  now also iOS. F1 menu showed Fullscreen/F11: menu.cpp `SR_PHONE` (Android or iOS) hides the PC options; no
  bug-report button on iOS yet. User: touch works, Fullscreen gone [confirmed, user]. Commit a5fc9c5.
  Android check of the same change: PC session's entry below [confirmed].
- Every iOS build now leaves `SonicRiders.ipa` in `~/Desktop/Sonic Riders iOS/` for Sideloadly (7e2ae1d).
- User: leaving the app reset it. Device crash logs: SIGSEGV in vkCreateDevice -> ~MVKDevice from
  device_recover: the game kept submitting GPU work in the background, iOS refused it (device lost) and the
  rebuild crashed. Android's SDL blocks the pump while paused; iOS's does not. Fix (render_vk.c,
  `ios_wait_in_background`): on SDL_APP_WILLENTERBACKGROUND (will resign active) wait for the GPU, then wait for
  SDL_APP_DIDENTERFOREGROUND. Tested [confirmed, scripted]: game running, devicectl opened Settings for 15 s and
  the game again: "[ios] paused" / "active again", no device loss, demo race at 60 fps (5000+ draws/frame).
  User: resume works [confirmed, user]; but under Control Center the sound went on. Log: during the wait the
  game core kept running (AX frames +1432 over a pause vs ~400 per normal interval; only drawing stopped), as
  Control Center makes the app inactive, not suspended. Fix: `rt_host_pause()` (frame_pacing.c) holds the game
  thread at its next frame end, `platform_audio_pause()` (audio_sdl.c) stops and empties the output; both from
  `ios_wait_in_background`. Tested [confirmed, scripted Settings switch]: AX +404 across the pause, 60 fps after.
  Control Center itself: not yet re-tried by the user.
- Unproven [uncertain]: a full race on touch, iPad, 7-day reinstall.


## 2026-11-07, night (PC "Mike"): iOS merge checked on PC and Android

- main 55896dc (iOS merge): PC build clean; det route to the title: frame identical to f42f30b except the FPS
  counter [confirmed]. main 9d7953e (SR_PHONE rename, touch on iOS): Android APK on the S23 with SR_SHOTS + adb
  taps: touch START, touch menu button open/close, phone menu without Fullscreen/resolution, a tapped option is
  saved [confirmed]. Stale remote branch `ios` deleted (user's request; its content is on main).
- User tried the PC build: above 4x the frame rate fell apart (their saved MSAA was 8x). Slider capped at 4x (4x =
  2112 lines, about a 4K screen's height; more was supersampling through a plain linear blit); a saved 5-6 loads
  as 4 [confirmed: SR_SCALE=6 run logs 4x].

## 2026-11-07, night (PC "Mike"): bloom and colour grading on PC

- render_gl.c `post_process`: after the last 3D (XF 0x26 = 0, perspective) draw before each XFB copy, the EFB is
  copied, bright parts (max channel above 0.5, soft knee) taken from a mip level, blurred (9-tap Gaussian, two
  rounds) at 1/4 and 1/8 of native size, screen-blended back, then graded (exposure, contrast, saturation,
  warmth); EFB alpha kept. HUD/menus drawn after it are untouched; frames without 3D (title) unchanged.
  Settings `bloom`, `grade_*`; F1 > Display Bloom + Colour presets (PC only); `SR_BLOOM` / `SR_GRADE` for tests.
- Tested [confirmed]: det route 1P race (5300) and 2P race (10500): bloom glow on bright track/windows, Vivid
  stronger colours, HUD sharp; title frame identical except FPS counter; 60 fps. First strength (threshold 0.6,
  mix 0.8) too faint, raised to 0.5 / 1.6. Not tested: the menu sliders live, MSAA + bloom together, a weak GPU,
  story cutscenes. Not accepted by the user yet.

## 2026-11-07, night (PC "Mike"): per-pixel lighting on PC

- The XF colour-channel lighting (`lights()`, `channel()`) moved from gx.vert to runtime/shaders/gx_light.glsl;
  tools/embed_shaders.py pastes it where gx.vert has `//@gx_light`, and into gx.frag only with PERPIXEL_LIGHT
  (defined for the GL programs only). With it, gx.vert passes view position, normal and raw vertex colours, and
  gx.frag re-evaluates lit channels per pixel for perspective draws when `u_perpixel` (setting `char_light`,
  F1 > Display, `SR_CHARLIGHT`) is on. configure.py lists gx_light.glsl as a shader input.
- Other platforms unchanged [confirmed]: all 17 generated sources identical after the move; after the change the
  Switch dksh header (uam) and the Vulkan SPIR-V header (glslc) are byte-identical to before. Specialized shaders
  (gen_shaders.py) read gx.frag without PERPIXEL_LIGHT: the new block compiles away [likely; not rebuilt].
- PC tested [confirmed]: det route, off = earlier frames exactly (except FPS counter); on: smoother shading on
  Sonic's quills, nothing else changed, 60 fps. Not tested: close-ups (character select, cutscenes), weak GPUs.
- Live resolution change (the F1 menu path, `rescale_copies`) tested with the new `SR_SCALE_AT` [confirmed]: 2x ->
  4x + 4x MSAA at frame 2000; title frames then equal a run started at 4x/4x, race frame differs by 0.01/255 mean.
- All PC picture options together (4x, 4x MSAA, bloom 50, Vivid, per-pixel lighting) on the det route through the
  1P and 2P demo races [confirmed, this PC]: no errors, lowest fps samples 58.0 / 58.9 (loading), HUD clean in
  both. Not yet seen or accepted by the user; nothing released.
- User played it (2026-11-07 late): bloom above 45% too much; colour presets barely noticeable; per-pixel lighting
  hard to see; choosing Character lighting "Original" showed Dear ImGui's "conflicting ID" error and highlighted
  Colour "Original" too (two radios with the same label = same ID). Fixed: `##light` IDs + PushID("grade");
  bloom mix 0.72 (100% = the old 45%); presets about twice as strong (Vivid 118/160, Warm 8/110/120/45, Cool
  112/110/-45) and warmth twice as strong per step. Menu test [confirmed]: SR_INPUT M.DOWN through the Display
  tab: focus on lighting "Original" alone, no error. Not yet re-checked by the user.
- User asked for iOS in the next release. Not possible as a download (an .ipa holds recompiled code); it needs a
  Mac setup script + `--flavor ios` zip + a cold Mac test first: to-do written at the end of docs/notes/ios.md
  for the Mac session (it was offline). Release on hold until then.

## 2026-11-08 (PC "Mike"): v0.6.0 released (iOS later)

- User: release now, iOS later; kept the fan-art launcher banner (asked: first release with the launcher).
  CHANGELOG v0.6.0 (iOS line: in progress, not in the release). Zips from dbeda548 (4); cold Windows Sandbox:
  PC PASS (2 + 1 min), Android PASS (2 + 2 min) [confirmed]. Published v0.6.0 (tag at dbeda548, Latest), v0.5.3
  release delisted (tag kept). Notes: build/release/notes-v0.6.0.md (from v0.5.3's, PC steps via the launcher).
- Not done: consoles/phone still run v0.5.3 dev builds; the user hasn't re-checked bloom/colour/menu fixes.
- Devices updated to main 77e7dc9 (= v0.6.0 + docs): OLED `/switch/sonicriders/sonicriders.nro` (previous kept as
  `sonicriders.prev.nro`; not boot-tested on the console), S23 APK installed over the app (data kept; boots:
  `[vk] ready`, touch layout). Switch folder cleaned at the user's request: old test .nro builds, RAM dumps,
  test env files and the dettest folder moved to `/switch/sonicriders/delete-me/` (FTP delete not available to
  the session; the user deletes it); small test data (recordings, logs, perftest json, shots, shader_keys) copied
  to build/old/switch_backup_20261108/ first. Kept: nro, prev nro, ISO, saves/, settings.ini, SonicRiders.nsp, log.
- **Open: Android abort at app exit** [confirmed once, S23]: the 9d7953e test build hit SIGABRT in
  `std::mutex::lock` (libmain+18069F0, helper thread) at 2026-11-07 20:10:45 while exiting (exit status 0), so
  the next start showed a "game crashed" report. Likely a thread locking a mutex already destroyed at exit
  (static destructor order). Not symbolized (that build was replaced). Report saved:
  build/android/run/v060/crash-report-9d7953e.txt. Next: reproduce (start, then leave/close the app), symbolize
  with the NDK llvm-symbolizer on the same build, stop helper threads before exit.


## 2026-11-08, later (PC "Mike"): Android crashes at app exit fixed

- **Crash 1 (new, reproduced first try on the S23):** opening the game, pressing Back (it goes to the background
  and keeps running), returning and removing its task (`cmd activity stack remove`, like a swipe from Recents)
  gave SIGSEGV at address 0x98: the render thread found the swapchain out of date, `swap_recreate` called
  `SDL_Vulkan_CreateSurface` after Android had destroyed the window (null ANativeWindow). Symbolized with the NDK
  llvm-symbolizer (`swap_recreate` <- `acquire` <- `present_picture` <- `efb_copy`). Fix (render_vk.c, Android
  only): no native window (`SDL_GetWindowWMInfo`) -> skip the frame. Tested: 5 closes in a row (3 straight, 2 with
  Back + return first) end with the normal system kill (signal 9), no crash report on the next start; Home and
  return still runs at 59.9 fps [likely: the crash happened once before the fix, so this is not proof].
- **Crash 2 (the 2026-11-07 report, SIGABRT in `std::mutex::lock`):** `exit(0)` on the render thread runs the
  C++ static destructors while the menu's helper thread still uses `g_mx`; bionic aborts on a destroyed mutex.
  Fix (menu.cpp): `g_mx` and `g_banner_lock` are never destroyed (like `g_cv`). Not reproduced: the
  `exit(0)` path (menu Quit, or SDL_QUIT) was not driven on the phone [likely, from the code and the report].
- PC: builds, starts minimized, closes normally (exit 0). Mac's fd3afff (iOS clock hold) also checked on PC.
- Local only: the user's new round icon (Downloads/SonicRidersFanIcon.png) for all local builds (NOTES.local.md).
- 32-bit Android (redditor request): looked at, not built (user waits for the phone model). Needed: an
  armeabi-v7a ABI in android_build.py/configure.py, a 32-bit SDL, a 512 MB guest window reservation that may not
  fit, crash-handler registers; no 32-bit test device (the S23 cannot run 32-bit apps).

## 2026-11-08, evening (PC "Mike"): bloom only in races; frame rate work started

- Bloom made character select almost white (user). Each frame now carries `in_race` (top mode = race); the
  GL post pass uses bloom 0 otherwise, grading stays. P1 route with SR_BLOOM=100: Player Entry normal [confirmed,
  frames]. Pushed 016497b. User likes the race bloom and stronger presets.
- Frame rate presets (user: 30, 45, 60, 120, 145, 180, 240, unlimited; 145 taken as 144). The game's physics are
  60 steps/s (its 30 fps mode = 2 steps per drawn picture), so above 60 needs in-between pictures. WIP branch
  `fps-interp`: pool of 4 frames, A and B held; draws paired by (prim, count, textures, position-matrix mask);
  paired matrices (12 words + normal 9) and projection lerped; rotation jump > 0.35 keeps B; vertices uploaded
  once per game picture; retire() when a frame is given back; vsync only when display Hz = target, otherwise a
  timer; FPS counter from the render thread (g_render_counts_fps). Tested: builds, 30 s intro at 120 [uncertain:
  no race yet].
- Custom music (Discussion #17, reddit): answered for the user; planned for the release after next.

## 2026-11-08, night (PC "Mike"): frame rate presets merged

- P1 race at SR_FPS=120 (fps-interp): 2 pictures per VI field, race clock 8.3 s per 1000 pictures; consecutive
  race frames change by a steady amount (no repeats or jumps); race start camera and HUD fine; >99% of draws
  paired [confirmed, frames]. Start-dash double images are the game's own blur (same at 60) [confirmed].
  Low FPS counter in dense screenshot runs = PNG saving, not the renderer.
- Presets in F1 > Performance (PC only) and the launcher (`pc_fps` saved by both; 30 = FR_30, 60 = pc_fps 0).
  Launcher SR_LAUNCHER_TEST "pc_fps=144" reached the game's renderer [confirmed]. Logs: measured rate in the
  `[gl] in-between` line, `PC pictures` in `[settings]`.
- Rates: 45 holds 45.0; unlimited at 4x+8x MSAA ~100-140/s, 2x+4x 250-350, 1x 400-1200 (RTX 3060) [confirmed]:
  GPU-bound, the menu note says to lower resolution. Merged to main 7487ffb with the Mac's iOS prep; a PC
  check run after the merge was slowed by another GPU user [uncertain]. Not accepted by the user yet.

- Hookshot (OoT Switch 2 -> PC, GPL-3.0) studied for ideas (user tip): notes/plans-2026-11.md "Ideas from
  Hookshot". Ideas only, no code (we are MPL-2.0).
- User played the frame rate presets: accepted ("about as close to eye candy as you can get without an HD
  remake"). Next: Hookshot ideas 2-4 + FSR sharpening.

## 2026-11-08, evening (Mac "Mac"): iOS verified on four devices, release prep

- "An error has occurred" (the GameCube disc-error screen) after 3 min with the iPhone locked: the time base kept
  running while the app was suspended (182 s jump), so the SDK's drive timeout alarm fired on waking (patch log
  80012398). Fix: `rt_clock_hold` (cpu.c), called by `rt_host_pause`: rt_ticks stands still while paused and
  carries on from the same value. User re-test: 3 min locked mid-race, no error [confirmed]. fd3afff.
- Touch (iOS only, touch_android.c `#ifdef SR_IOS`): unit in points (iPhone 340, iPad 390) instead of the screen
  height (the iPad had twice the phone size), bottom controls lifted, right cluster 0.07 unit in, START 0.5 unit
  from the right edge (Control Center swipe) [accepted: "controls felt good", 1-lap iPhone race].
- Races on touch, from device logs [confirmed]: iPhone 17 Pro 59.9 fps (GPU 2.5-3 ms); iPad Pro 11 M1 59.5
  (2 ms); iPad A16 59.3 (3.5 ms; hitches only first-use pipelines); iPad 7th gen (A10) ~57 avg, dips to 40, GPU
  ~10 ms avg / 33 max, 91 hitches with "GX draws" 40+ ms (not pipelines, not heat) [likely GPU-bound].
- Release prep: tools/setup_mac.py + make_release --flavor ios + INSTALL/README; ios_build.py finds the team
  from Xcode's account list, no team = unsigned .ipa (Sideloadly). Cold test from the zip (fresh folder +
  tools, --unsigned) PASS in ~2 min [confirmed]. Sideloadly install on the A16 iPad: black screen = (1) the
  Desktop .ipa predated the disc message, Sideloadly's new bundle ID gave an empty Documents; (2) the next one
  hung at the loading icon after the .iso was dragged in, booted fine when restarted [likely: opened mid-copy].
  Fixes: .iso also found at the top of Documents; size check (incomplete image message); the message box needs a
  window attached to the scene (ios_attach_window) or it waits unseen on black [confirmed on the iPad Pro, user].
  Default app icon = the save icon from _Main.rel, 32x nearest [confirmed, user].
- Facts: a free Apple ID allows 3 sideloaded apps per device (install error MIInstallerErrorDomain 13); devicectl
  `device capture screenshot` gives black frames on the iPad Pro; rt_log flushes at most once a second
  (SR_LOGSYNC=1 for every line).
- Not tested: a Mac without Homebrew/Xcode; the signed install route from the zip.
- Renewal with Sideloadly on a Mac-installed app (iPad Pro): with Advanced Options bundle ID = the app's own ID it
  replaced the app in place, disc image and saves kept [confirmed]. Without it Sideloadly adds the team ID
  (a second empty app, seen on the A16 iPad). Docs + setup: "renew the same way you installed", the app ID in
  "How to renew (every 7 days).txt" next to the .ipa.


## 2026-11-08, late night (PC "Mike"): Hookshot ideas 1-4 (background pad, dedicated GPU, 5 logs, FSR 1)

- Controller in background: setting `pad_background` (default on), SDL hint `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS`
  set before SDL init and re-set when changed (render_gl.c `sync_fullscreen`); F1 > Controls + launcher Controls.
  Compiles; launcher -> game round trip (pad_background=0 reached the game's `[settings]` line) [confirmed]; not
  tried with a real pad and another window focused [uncertain].
- Dedicated GPU: `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` exported from sonicriders.exe
  (platform_host.c; llvm-objdump lists both). `[gl]` still names the RTX 3060 [confirmed]; the desktop has one GPU,
  so the Optimus switch itself is untested (needs the gaming laptop) [uncertain].
- Five logs on PC (platform_host.c, no-console start) and Linux (`Sonic Riders.sh` from setup_linux.py):
  sonicriders.log, .prev.log (names kept for iOS / tools/ios_run.py, Mac checked), .prev2-.prev4. Four starts via
  Start-Process rotated correctly [confirmed]. Bug-report zip lists all five (not run) [likely]. iOS/Android
  branch, iOS stderr and log.c flush untouched.
- FSR 1: AMD's ffx_fsr1.h (MIT, notice kept) ported to GLSL 3.30 (runtime/src/platform/host/fsr1_gl.h: texelFetch
  instead of textureGather). `fsr_present` replaces the linear present blit when `sharpness` > 0: EFB part copied
  upright, EASU to the window's picture size if bigger (else linear), RCAS into the window (100% = AMD's max,
  -1 stop per 50%). Slider in F1 > Display and the launcher Graphics page; `SR_SHARPNESS`. HUD is sharpened too
  (part of the EFB). Det-route P1 race at 1x/2x and 0/50/100%: right orientation and colours, crisper edges at 1x
  (EASU), no halos; 100% brings out texture grain [confirmed, frames]. 120 preset holds 119.9-120.1/s at 3x+2x
  MSAA with 100% sharpness; in-between pictures sharpened fine [confirmed]; unlimited rates too noisy to show the
  cost. Launcher round trip sharpness=40 [confirmed]. Not played by the user yet.
- Bug found while testing: a scripted edit to the launcher's settings_save missed silently, so the launcher
  dropped the new keys; fixed and retested.
- Follow-up: the game's own settings_save also missed the two new keys (a scripted edit that did not match;
  the -Wformat-extra-args warning was hidden by a too-narrow grep of the build output). F1 changes to sharpness /
  background pad applied in the running game but were not saved. Fixed; settings.c compiles clean [confirmed];
  the exe not relinked yet (a game was running). Lesson: assert every scripted replace; grep builds for "warning".
- User played FSR sharpening + the background pad option: **accepted** ("I absolutely love it"). Exe relinked with
  the save fix after the game closed (clean build, 0 warnings). A scripted F1 toggle to prove the save did not
  change any option (menu focus), so save + restart stays unproven by run [likely].
- "Still copying" message (ef6c112): tested by a devicectl copy on the iPad Pro (0 -> 4.7 MB in 1.5 s) and by the
  user with a Finder copy on the A16 iPad [confirmed]. The user's first attempt showed nothing because the Desktop
  .ipa had not been rebuilt after the visible-message fix (rebuild the Desktop .ipa after every runtime change).
  Finder after an app reinstall: "The required file cannot be found" until refreshed [confirmed, user].


## 2026-11-08, night (PC "Mike"): v0.7.0 released

- Zips from c9a2f98 (PC, Android, Switch, Linux); cold Windows Sandbox PC PASS (2 + 1 min), Android PASS (2 + 2 min)
  [confirmed]. Tag v0.7.0 at c9a2f98, published as Latest; v0.6.0 release delisted (tag kept). iOS zip: Mac makes,
  cold-tests and uploads it to the release. GBAtemp post draft written for the user (not in the repo).

## 2026-11-08, late night (PC "Mike"): freeze while recording with ShadowPlay; stale build label

- User: game froze after character select, while loading a race, with NVIDIA ShadowPlay recording (Alt+F9). The
  normal PC run writes no log. Retest with `SR_LOGSYNC=1 SR_DVDLOG=1`, ShadowPlay recording, a full race: no freeze,
  only a few 50-70 ms hitches (waiting for the render thread). Not reproduced, not fixed [uncertain]. The renderer
  uses one GL context on one thread, so the known multi-context overlay conflict does not apply. Suggested OBS Game
  Capture for clips; if it freezes again, catch the hung process with logging on.
- The log's `[build]` line said v0.6.0-4 on a v0.7.0 checkout: gen/version.c was written only by configure.py.
  Now `tools/write_version.py` also runs before every ninja build (restat: an unchanged id rebuilds nothing;
  checked: second ninja run does only VERSION) [confirmed]. Release zips were already right (VERSION file); no new
  release, per the user.

## 2026-11-09: update notice on PC (launcher + game), issue #18

Issue #18 (game stuck on Player Entry in Grand Prix) came from v0.6.0 although the form said 0.7.0; the reporter
was asked to update. User's request: prompt anyone on an older version. Implemented `runtime/src/update_core.h`
(header shared by the game and the launcher: WinHTTP GET of api.github.com releases/latest, `tag_name`, compare
vX.Y.Z), `update_check.c` (helper thread at start-up; banner for 20 s; F1 > About line, download button, setting
`update_check`), launcher sidebar button + About line. Windows only; Switch, Android, Linux and iOS do not check.
- Tested [confirmed, frames]: game labelled v0.6.0 -> log "[update] v0.7.0 is available", banner on screen,
  F1 > About shows it; launcher as v0.6.0 shows "Update available: v0.7.0" + "Download update", as v0.7.0 nothing.
- User's decision: ship it in the v0.7.0 PC zip (assets replaced, release description notes it). Players already
  on v0.7.0 get it with the next download; it only prompts from the next release on.
- Same day: new launcher icon `launcher/icon_256.png` + `icon_32.png` (pinned in OWN_ART) (Super Sonic art made by the user with ChatGPT; user's choice for
  the public build); PC zip rebuilt and re-uploaded to v0.7.0. Icon in the .exe checked [confirmed].

## 2026-11-09: Switch / Android installer window (launcher in installer mode)

User's request: the Switch and Android .bat installers as a window like the PC launcher. The launcher exe, named
`Install Sonic Riders (Switch|Android).exe`, becomes an installer (notes/launcher.md "Installer mode"): disc image,
SD card (auto-found) or phone, own icon, Switch HOME-screen icon + prod.keys, Android SDK licence; runs setup.ps1
with -Yes and the answers. The .bat stays as "... - command line.bat".
- Tested [confirmed]: pictures of both modes and the PC launcher unchanged; a full Switch build through the
  installer (SR_LAUNCHER_INSTALL, no SD card): PASS, setup exit 0, .nro built. Not tested: copying to a real SD
  card, the HOME-screen icon, an Android build/install through it, a cold Sandbox run (user chose to replace
  the v0.7.0 zips directly).

## 2026-11-09: v0.7.1 (installer SD-card fix, bigger launcher text)

User report (picture): the Switch installer failed at step 2 with "Test-Path : Illegal characters in path" after
picking the SD card F:. Cause: the path was passed as "F:" + backslash + quote; a backslash before the closing
quote escapes it, so setup.ps1 got `F:"`. Fix: `qpath()` quotes every path and adds "." after a trailing
backslash. Tested [confirmed]: full Switch build through the installer to a test SD folder given with a trailing
backslash: PASS, .nro copied and verified. Also: fonts 17/14/40/28 -> 20/16.5/40/28, window 1280 x 820 (clamped to
the work area), shorter checkbox labels with notes, SD picker sized to its column (pictures of all modes).
Released as v0.7.1 at the user's request (no Sandbox run; iOS zip made here from the same source).

## 2026-11-09: in-launcher updates with release notes (v0.7.1 zips replaced)

User's request: launchers/builders show the changelog and download the update right there. Launcher + Switch /
Android installers: Update page with the release notes and DOWNLOAD AND INSTALL (download, unpack over the folder,
restart; PC: Rebuild prompt). Game: release notes in F1 > About (Windows). Not on the consoles yet: the Switch
build has no TLS library; the Android app has no check (a later step: Java HTTPS, notice only, since a new APK
must be built on the PC). Tested [confirmed]: Update page picture with the real v0.7.1 notes; update test PASS in
an unpacked PC zip and Switch zip (real 3 MB download, exe swapped to .old, saves kept); F1 > About picture with
the notes. Found on the way: the exe crashed at exit (an update reader thread left joinable) and the marker folder
was missing; both fixed before the release. No Sandbox run (user: replace v0.7.1 directly).

## 2026-11-09, Mac launcher for the iOS builder (Mac session, request relayed from the PC session)

- Asked: make the iOS .command a launcher like PC/Android/Switch, custom icon from any picture, fix the missing
  execute bit, make the Gatekeeper first start easy.
- Execute bit: v0.7.1's iOS zip already stores the .command as 0755 with Unix origin; cold Archive Utility
  extracts with and without a quarantine flag kept 0755. Not reproduced [uncertain what the user hit]. macOS 27.2
  shows downloaded *scripts* a separate "File Not Opened ... Scammers often encourage ... running scripts" dialog
  (Quarantine.loctable), a likely cause of the trouble [likely]. Zip entries now also carry S_IFREG.
- Built: launcher/mac/SonicRidersLauncher.swift -> Sonic Riders Launcher.app (tools/build_mac_launcher.py, ad-hoc
  signed after assembly), shipped by make_release.py --flavor ios with START HERE (Mac).html; setup_mac.py --icon
  any picture (sips centre square, flatten_png now 8/16-bit, grey/palette). Launcher removes quarantine from the
  whole folder at start; finds its folder through App Translocation.
- Tested: icon conversion PNG/JPG/HEIC/GIF/BMP/TIFF, 16-bit RGBA, transparent GIF [confirmed; WEBP read not tested,
  no writer to make one]. Quarantined binary run from a shell is killed (exit 137) until approved [confirmed].
  Cold zip -> launcher build -> .ipa with the HEIC icon (1024 px AppIcon in Assets.car) [confirmed]. Pages checked
  by off-screen pictures (SR_LAUNCHER_SHOT).
- Unproven: the four Gatekeeper clicks done by a person (drawn dialogs use macOS's own strings; the last
  confirmation dialog's wording not seen), Install/Renew pages with a real device, a Mac without Xcode/Homebrew.
- Later: the user ran the zip's first start on macOS 27.2 and sent screenshots. Real order differs from the
  first drawings: in the "Not Opened" alert the blue default button on top is Move to Trash (Done below, grey);
  after Open Anyway in Settings a second alert "Open “X.app”?" has Move to Trash / Open Anyway / Done, then Touch
  ID or Use Password, then the launcher asks for Downloads and Desktop access. START HERE, INSTALL and README-FIRST
  redrawn / reworded from the screenshots [confirmed, user].

## 2026-11-09: v0.7.2 released (Mac launcher)

User: "Works! go ahead and new release v0.7.2" after the first start of the Mac launcher from the test zip. Tag v0.7.2 on
8045090; PC, Switch, Android, Linux zips from that commit. The in-launcher update code (in the replaced v0.7.1 zips) had
never had a cold Sandbox run, so the user chose to run it first: PC PASS (2 + 1 min), Android PASS (2 + 2 min), both with
a normal path and a path with spaces [confirmed, cold]. Note: while the Sandbox maps build/release, new zips cannot be
renamed into it (WinError 32); use `--out` to another folder until it closes. v0.7.1 release delisted (tag kept). iOS zip
from the Mac session. Not tested: updating from v0.7.1 to v0.7.2 through the launcher's Update page [uncertain].

## 2026-11-09, v0.7.2 iOS zip (Mac session)

- SonicRiders-iOS-Setup-v0.7.2.zip made with `make_release.py --flavor ios --ref 8045090` (with `--ref v0.7.2` the
  VERSION file names the annotated tag object 5e2b7ed0, not the commit the other zips name; same tree). Cold test:
  quarantined zip, Archive Utility extract, launcher + .command 0755, codesign valid, spctl "rejected" (the Open
  Anyway route); launcher-driven build from that folder, 7/7 steps -> .ipa [confirmed]. Not covered: fresh tool
  downloads (the launcher uses the cached tools folder), install on a device from this zip. Uploaded to the release.

## 2026-11-09: v0.7.3, launcher update never replaced VERSION

User report: on a v0.7.1 Switch installer folder, DOWNLOAD AND INSTALL downloaded and asked to restart, and after the
restart the update was offered again. Found in the folder: CHANGELOG was v0.7.2 but VERSION still "v0.7.1 (9a97e9a0)".
Cause [confirmed]: apply.ps1's robocopy skips files whose size and time match; make_release stamped every entry
2026-01-01 and VERSION is 18 bytes in both versions. Any same-size changed file was skipped the same way. Fix: robocopy
`/IS /IT /IM` (/IS /IT alone still skipped it: newer Windows classes it "modified"), and zip entries carry the commit
time. Tested by script with the real zips [confirmed]: old script + v0.7.2 zip on a v0.7.1-state folder reproduces the
bug; new script + same zip -> v0.7.2, 184/184 files equal; old script + v0.7.3 zip on a v0.7.2 folder -> v0.7.3,
184/184 equal (so released launchers update correctly). Cold Sandbox PC + Android PASS. The user's folder was finished
to v0.7.2 by hand. Not tested: the click-through in a real launcher from v0.7.2 to v0.7.3 [likely fine: same script].
- Same day, found by the Mac session: the commit time in the zips is UTC, but zip times carry no time zone and are
  read as local time, so west of UTC the files were dated hours in the future and ninja redid 87/87 steps after a
  build until the clock passed it. make_release now stamps commit time minus one day. The four v0.7.3 zips were
  rebuilt from 6db6158 and replaced on the release (contents identical except the launcher exe's 2-byte PE
  timestamp) [confirmed]. iOS zip: the Mac session uploads it after the user approves.

## 2026-11-09, v0.7.3 iOS zip (Mac session)

- Built from 6db6158 and cold-tested (quarantined Archive Utility extract: 0755 kept, codesign valid, spctl
  "rejected" = Open Anyway; launcher-driven build 7/7 -> .ipa) [confirmed]. Found: the first commit-time stamps
  were UTC written as zip DOS time (no timezone), so files extracted west of UTC were dated in the future; on this
  Mac (EST) `ninja -n` right after a full build still wanted 87/87 steps [confirmed]. make_release now stamps
  commit time minus 1 day (PC session). iOS zip rebuilt with the fix (entries dated 2026-11-08 23:06:50) and
  uploaded with the user's OK; the release asset is byte-identical to the local zip [confirmed]. The Mac side has
  no size+time-skipping copy step (no self-update; ditto into a fresh folder; shutil copies always write).
- Accepted: the user clicked DOWNLOAD AND INSTALL in their v0.7.2 Switch installer folder and got v0.7.3 ("worked
  beautifully") [confirmed, user].
- Same day: repo description now names every platform; README banner `docs/images/banner.jpg` (fan art, unknown
  artist, credit line asks them to open an issue; pinned in OWN_ART). User back Thursday 2026-11-12.

## 2026-11-09, iOS pads and app IDs

- iPad (Xbox One and PS5 pads): A / Cross paused the race instead of jumping [confirmed, user]. Cause [likely, not
  yet checked in the SDL source or on the device]: iPadOS also sends pad buttons as UIKit presses; SDL2's uikit view
  maps Select to Enter, and the keyboard's Enter is START in `input_sdl.c`. Fix: under `SR_IOS` the keyboard is
  ignored while a controller is open. SDL 2.32.10 `scancodeFromPress` checked by the Mac (Select -> RETURN) [confirmed].
  **Accepted**: iPad Pro 11" 3rd gen, Xbox One and PS5 pads, build v0.7.3-9-gee3aaa14: "Both controllers now work as
  intended" [confirmed, user]. Not released yet.
- Updating with Sideloadly made a second app (new bundle ID, empty Documents) on the iPhone and the iPad
  [confirmed, user]. The old apps were Xcode-installed (`net.srnative.sonicriders.t<hash>`); Sideloadly's default
  appends its team ID. The iPhone took the update with the old ID entered by hand. The Mac backed up all saves (iPhone, 3 iPads).
  Device IDs differ (iPad Pro plain `net.srnative.sonicriders`, others `.t<hash>`). Plan (Mac, own commit): reuse an
  installed app's ID; new installs use `<ipa ID>.<TEAMID>` = Sideloadly's default; Files-app save-copy section in INSTALL.md.
- Stable app IDs (Mac). Sideloadly's default turned out to ALWAYS append ".<team>": an .ipa already carrying
  `net.srnative.sonicriders.<team>` became `...<team>.<team>` (iPad Pro, user ran Sideloadly) [confirmed]. So the plan
  changed: signed builds default to `<base>.<team>` and the .ipa gets the plain `<base>` ID (ios_build.ipa_bundle_id,
  make_ipa rewrites Info.plist); setup_mac.py picks the device before building and reuses an installed
  net.srnative.sonicriders* ID (else the last build's); renew notes say "defaults" or "untick" to match; INSTALL.md
  has "Ended up with two Sonic Riders apps?" (Files app). Tested: devicectl install of `<base>.<team>` on the iPad
  Pro, then Sideloadly with defaults and the `<base>` .ipa -> still one test app [confirmed]; both test apps
  removed afterwards, the user's app, save and disc untouched. setup_mac's new functions checked against the iPad
  Pro (plain ID), iPhone (`.t<hash>`) and no device (last build's ID) [confirmed]; a full setup_mac run (cold
  test) not done yet [uncertain]. Older `.t<hash>` installs keep their ID (Sideloadly: untick).

## 2026-11-10: v0.7.4 (iOS controllers, stable app IDs)

Released: tag v0.7.4 on 039bd81, five zips (PC, Switch, Android, Linux, iOS from the Mac). Contents: the iOS pad fix
(pad A paused the race; accepted on the iPad Pro, entry above) and stable iOS app IDs. The cold Sandbox test was
skipped at the user's request, so this release was not tested cold [not tested].

## 2026-11-10: GitHub shows the last 5 releases

User: "I want people to be able to see the last 5 releases from the most recent one" (replaces the only-latest rule).
v0.7.0, v0.7.1, v0.7.2, v0.7.3 re-created from their kept tags with `--latest=false` (v0.7.4 stays Latest, so the
launcher's update check is unchanged), notes from CHANGELOG. Zips re-uploaded from build/old and build/release; each
zip's VERSION names its tag's commit [confirmed]. Not restored: v0.7.1's zips (not kept; release has notes only) and
the iOS zips of all four (made on the Mac). From now on: publish the new release, delete the one that falls to 6th.

## 2026-11-10 (night): picture options for every platform, plan only

User's task: the PC F1 > Display set on Linux/Deck, Android and Switch (replaces "PC only"). Session ended early
(model change) with the survey and plan done, no feature code. Findings: Linux builds the same render_gl.c and menu
block, so it already has everything [confirmed, code]; Android/Switch have only 1x/1.5x/2x; MSAA and per-pixel
lighting are the large items (lighting needs lit variants of the specialized shaders). Order agreed by the user:
Linux check, shared menu, Android (resolution, FSR 1, colour, bloom), Switch the same, then MSAA, then lighting.
Design: notes/picture-options-port.md. Linux: the WSL run with captures was started and skipped at the user's request [not tested].

## 2026-11-10/11 (night): picture options on Linux, Android, Switch; fan icon; phone menu scrolling

- Menu: `PIC_*` defines per platform in menu.cpp (an option shows only where its renderer does it). PC/Linux all;
  Android and Switch: resolution 1x/1.5x/2x, Sharpness (FSR 1), Bloom, Colour. MSAA and smooth character lighting
  stay PC/Linux (MSAA needs a resolve before every EFB copy; lighting needs lit variants of every specialized shader).
- Linux [tested]: WSL build, perf-test route, off vs 3x + MSAA 4 + bloom + FSR + lighting + Vivid: frames right.
- Android (render_vk.c) [accepted: "Absolutely gorgeous on my phone. FPS was more than acceptable. Approved."]: S23
  Ultra, 1x, fit-window: demo 1P 59.4 fps off / 57.9 with bloom 60 + Vivid + FSR 60 (854x480 -> 2562x1440); GPU about
  +8 ms in menus (FSR at 1440p), +2-3 ms in races. Menu taps (adb, wireless) change and save the options.
- Phone menu could not be scrolled by finger (the Display tab is taller than the screen): `touch_scroll()` (a
  vertical drag scrolls and lets go of the widget it started on) [tested, adb swipes; accepted with the above].
- Switch (render_deko.c, Vulkan quad shaders built for deko3d with `QUAD_DK`, uniform buffer 3) [accepted: 60 and
  pretty for the most part, dips in heavy areas; 2x suits 30 fps]. OLED handheld, overclocked 1785 / 614 / 2133,
  1x, demo 1P GPU per frame: off 6.9 ms, bloom + Vivid 9.0 ms, + FSR 13.2 ms; game 59.94 fps in all (switch-remote
  perf-test runs). Hardware bug found: FSR's integer clamp + texelFetch read only the first column on the console
  (horizontal stripes; fine in Eden and on Vulkan); float coordinates + nearest sampler fixed it. All dksh
  `per_warp_scratch 0`.
- Game icon: cedry2kio's fan-made round icon (DeviantArt) on a black background, `setup/icon.png` (+ 256 JPEG for the
  NRO), the default in setup.ps1 / setup_linux.py / configure.py; README credit; guard allow-list (user's OK).
  The launcher keeps the AI art icon.
- `SR_BLOOM` / `SR_GRADE` moved to settings.c (all platforms); `SR_RES=0-2` added.
- Released v0.8.0 (tag on 54997c5; PC, Switch, Android, Linux zips; iOS to follow from the Mac). Cold Sandbox: the
  first PC run FAILED in setup step 2 (the new default icon's PNG -> ICO step wrote into build\ before it existed;
  that path only ran for a player's own picture before) [confirmed, setup log]; fixed in 54997c5, PC PASS (2 installs,
  one with a space) and Android PASS [confirmed]. v0.7.0 delisted (tag kept); reply posted in Discussion #19.
- Switch SD: sonicriders.nro = the v0.8.0 code (+ NRO icon); new HOME-screen forwarder with the fan icon as
  /switch/sonicriders/SonicRiders.nsp (user installs it with DBI); leftovers moved to /switch/sonicriders/_delete-me.
- PC check before the release: build OK; perf route with bloom + FSR + Vivid 59.94 fps, frame right [confirmed].

## 2026-11-11 (morning): game icon swapped to cedry2kio's "Sonic Riders Icon 3"

- User's choice: the same artist's other icon (DeviantArt "sonic_riders_icon_3"), transparent outside filled with the
  blue the iOS build showed (RGB 12, 62, 144). `setup/icon.png` (512) + `setup/icon_256.jpg`, guard hashes updated
  (1a195a4). Launcher icon unchanged; README credit unchanged.
- v0.8.0 PC/Switch/Android/Linux zips rebuilt from 1a195a4 and replaced on the release (tag stays on 54997c5);
  downloaded PC zip has the new icon [confirmed, hash]. Original zips kept in build/old/release-v0.8.0-first-icon/.
  Not rebuilt: the user's Switch SD forwarder NSP and NRO still show the old icon until rebuilt.

## 2026-11-11, Mac: v0.8.0 iOS zip, bloom on character select

- Picture options on for SR_IOS (menu.cpp, the Android set on render_vk.c / MoltenVK) and the fan icon as the only
  default iOS icon, no save-icon fallback (matches ca93fdc): 42c9217. Perf route [confirmed, logs], options off vs
  SR_BLOOM=60 SR_SHARPNESS=60 SR_GRADE=0,118,160,0: iPad Pro 11 M1 (wired) 1P 59.5 / 2P 59.9 fps -> 59.9 / 59.9;
  iPhone 17 Pro 59.9 / 59.9 -> 59.9 / 59.9 (game 0.9 -> 1.0 ms per step), FSR 854x480 -> 2146x1206, all states
  specialized. Frames look right (different moments, no SR_DETCLOCK: not a pixel diff). One iPhone 1P shot with
  options on has a big white haze, probably game dust made brighter by bloom + contrast [uncertain].
- Bloom lit character select on the iPad (user): GAME_TOP_MODE stays 8 there. in_race now also needs fewer than 8
  frames in a row with GAME_MENU_FLAG set (results screen keeps bloom): 00d3861, shared by all renderers. iPad: no
  bloom on character select, bloom in races [confirmed, user]. PC route check: PC session. Not in the other
  v0.8.0 zips.
- User played the iPad Pro build on max settings: "runs so so well ... flawless" [accepted, iPad Pro]. iPhone: perf
  route only. Finger scrolling: not checked separately.
- SonicRiders-iOS-Setup-v0.8.0.zip from 00d3861 uploaded to v0.8.0, the notes' "later today" line replaced
  (user's OK) [released]. Not done: a cold Mac test from this zip.
- Device notes: devicectl install with a different bundle ID = a second app (empty Documents): build with
  `--bundle-id <installed ID>` (`devicectl device info apps`; the test devices' IDs: NOTES.local.md). Sideloadly:
  untick "automatic bundle ID" and type that ID. iPhone wireless
  link failed (CoreDeviceError 4000 "Can't assign requested address", also on cable) until an Xcode update and a
  phone restart; error 4016 = not unlocked since restart.

## 2026-11-11 (PC "Mike"): icons, bloom check, v0.8.0 zips replaced again

- Save icon never a default any more (user): setup.ps1 no longer writes build\icon.ico from _Main.rel; configure.py
  makes build/icon_default.ico from setup/icon.png (default_pc_icon); setup_linux.py and the Switch forwarder default
  to setup/icon.png (ca93fdc); iOS the same on the Mac (42c9217).
- PC check of the Mac's bloom fix 00d3861 (SR_BLOOM=100, scripted 3-lap Metal City Free Race, minimized): Player
  Entry before the race and Normal Race / Track Select / Player Entry after "Back to Menu" without bloom (post-race
  Player Entry mean RGB equal to pre-race), race and results screen with bloom [confirmed, frames].
- User: keep it on v0.8.0. PC/Switch/Android/Linux zips rebuilt from be45877 and replaced on the release (tag stays
  on 54997c5); cold Sandbox PC (2 installs, one with a space; fan icon in the log) and Android PASS [confirmed];
  downloaded PC zip = built zip [confirmed, hash]; release notes got a bloom line. Old zips in
  build/old/release-v0.8.0-before-bloomfix/.
- Guard: PAST_ART history allow-list from the Heroes repo (a09bbbb, user's request) as 71bcfd2, committed with the
  noreply identity; GitHub's history scan green again (it had failed on old versions of the icons and stick knob).
- S23: APK v0.8.0-4-g4a24700a installed over USB. Next-session prompts no longer go in STATE.md (user).

## 2026-11-12 (PC): 999 ring cap, launcher icon text

- Launcher step 3 named the memory card picture; now the fan icon by cedry2kio (7bc8a74). PC/Switch/Android
  installer modes opened for the user (SR_INSTALLER=switch|android).
- Ring cap: scan of build/gen for `cmpwi/cmplwi x,100` + `li x,100`: rings at racer + 0xB98 (stride 0x1080). Sites:
  per-frame racer loops 81831A78 / 8182B828 / 81835F28 (`if > 100 = 100`), branchless min 81815CA8/CB0, HUD rolling
  counter 8182BC20 / 81830628 / 81831CF8. 81973F4C / 81974AF0 are percentages (byte at 81AB7A1C), not rings.
  Pickups (+10/+20/+30 boxes 81889EB8.., 8188A658, 8195F7B0) write the racer's own word; no team/partner count.
- Test: scratchpad ringrun.sh (full-race route to field 20000, SR_POKEAT=14000:81A0E2D8:000000FA, SR_PEEK).
  999: 250 -> 279, HUD 3 digits; vanilla: 250 -> 100. A first vanilla run run in parallel stopped logging at field
  ~3750 (menus); alone it was fine [uncertain why].
- settings.ini never saved `update_check` (format string ended at sharpness): fixed with the new key.
- User: the next release waits for the Heroes alpha (notes open with a call for Heroes alpha testers).
- Later: menu label "Ring Limit set to 999"; with it a boost hit (fn 81849394, store 81849410) takes 100 rings,
  not all; the max-level item box (case 8188A0E8) no longer lowers rings above 100. Found with temporary
  `[ringdbg]` patches on every store to racer 0's ring word: 81817A58 (fn 81817264) is the fall-off-course
  respawn, rings -> 0 (seen in shots: fall, black fade, respawn). User chose 100 for falls too (patch keeps the
  old count before the store, r6 = 0 also clears other fields). PC run poked at 9500: 258 -> 158 -> 58 -> 0.

## 2026-11-12 (PC, later): v0.8.1 released with the Sonic Heroes alpha call

- User announced the Heroes alpha build and asked to release 0.8.1 with the call for alpha testers. Done: release
  notes open with it (link to the Heroes repo main page), README line 1 "Alpha testers needed:", the Windows
  launcher sidebar (all modes) gets the message + a "Sonic Heroes recomp" button [confirmed, SR_LAUNCHER_SHOT in
  PC and Switch modes], Linux and Mac setup print it at the end, Mac launcher About page [implemented, not built:
  needs a Mac]. Cold Sandbox PC + Android PASS (2 installs each) [confirmed]. Zips: PC, Switch, Android, Linux;
  no iOS zip (make_release --flavor ios builds the Mac launcher, only on a Mac).

## 2026-11-13 (Mac): v0.8.1 iOS zip; one app ID per player on iOS

- v0.8.1 had no iOS zip (needs a Mac). Built with `make_release.py --flavor ios --version v0.8.1 --ref v0.8.1`,
  cold Mac unzip test PASS (unsigned, fresh tools dir, 7/7 steps) [confirmed]; launcher About page shows the Heroes
  call [confirmed, shot]; uploaded to the release (user's OK). Bug found: an annotated tag as --ref stamped the tag
  object's hash (04573a6b) in VERSION; fixed in e52dad9 (`ref^{commit}`), rebuilt zip differs only in VERSION.
- Installing on the user's iPhone failed with "integrity could not be verified" (MIFreeProfileValidatedAppTracker):
  the build's ID (`<base>.<team>`) was new there and the free account's 3 apps were taken. Every device had another
  ID (`<base>` from unticked Sideloadly, `<base>.t<hash>` from pre-2026-11-09 Mac installs). User's plan: one ID
  for everyone. The tools always build `<base>.<team>` (Sideloadly's default makes the same); `ios_run.py install`
  moves an older-ID app (Documents copied to the Mac and checked, old app removed, new installed, copied back and
  checked); `status` / `devices --tsv` feed the launcher's Install page (cable device first, full names: names were
  cut at the first space before); `push-iso` skips a disc already there; About tab shows the App ID.
- Tested on an iPad (A16) over the cable: move `.t<hash>` -> `.<team>`, 7 files incl. the 1.4 GB .iso, card_a.raw
  MD5 equal, the game boots with the save card, About tab shows the ID [confirmed]; update path, push-iso skip,
  signed setup_mac.py run, launcher Install page line [confirmed]. Unproven: the old-ID warning on screen, a move
  at the 3-app limit, a Sideloadly renewal after a move [uncertain].
- Later: the user's iPhone moved over the cable (23 files incl. the disc, card_a.raw MD5 equal) [confirmed]; the
  iPad Pro (in use) updated in place under its old plain ID over the network, no launch. v0.8.1 release notes now
  list the iOS zip. v0.8.2 tagged on 13b02c5 (user: "release v0.8.2 with the iOS app ID fix"); iOS zip cold Mac
  test PASS [confirmed]; draft release with the iOS zip, PC session asked to add the other zips, run the Sandbox
  test and publish. Note: macOS's python3 is 3.9; make_progress.py needs 3.10 (`write_text(newline=)`).
- Then the user changed their mind ("i want this on the current release"): v0.8.2 draft and tag deleted (never
  published), PC session told to stop; CHANGELOG entries folded into v0.8.1; v0.8.1's iOS zip rebuilt from
  abedb6a (`--version v0.8.1 --ref HEAD`), cold Mac test PASS, launcher About "v0.8.1 (abedb6a0)" [confirmed],
  uploaded with --clobber; release notes got an "iPhone / iPad" section on how the one app ID works.

## 2026-11-13 (PC, Mike): gear Lv. 4 and Lv. 5; v0.8.1 preview

- User asked for Lv. 4/5 past the original 3 with the 999 ring limit; values: Lv. 4 at 200, Lv. 5 at 500, Blue
  Star 210/300 and 240/350 (top/boost), others scaled from their own Lv. 3. User's playtest of v0.8.1: capsule
  +100 and hit/fall -100 "worked perfectly" [accepted].
- Found statically (dtk asm) and checked in RAM dumps: level byte racer +0x102E, thresholds {30, 60, 255} at
  819C2B94, three 0x38 stat blocks from +0x8DC (top +0x08, drift dash +0x30, boost +0x34; Blue Star matches
  WtR_Flash's GameFAQs guide exactly) [confirmed]. No room for blocks 4/5 and ~100 readers of the byte, so the byte
  stays 2 and block 3 is raised/restored natively (game_levels.c, hooks in the racer loops) [confirmed, PC].
- The player's level-up is a fourth copy (fn 81836054, r31); the first three loops never saw racer 0. Replaying
  it with the byte shown as 1 for one check gives the game's own LEVEL UP!! panel, sparkles and air refill
  [confirmed, frames + RAM]; sound not heard (muted runs) [likely].
- HUD digit: the first guess (entries 0x87.., fn_1_2B3B8) was the wrong HUD; poking the REL table did nothing
  because the HUD reads a heap copy (RAM dump search). The 1P HUD (fn_1_31670) uses entries 0x12 + level (16x16
  row at the atlas top); a texture-pixel swap attempt showed nothing because of that. Now: "4"/"5" drawn into empty
  atlas spots at decode from the orange digits, cell pointer redirected per racer to entries at 0x80001F00
  (zero in RAM dumps) [confirmed 1P, frames]. Split-screen hooks unverified [uncertain].
- Speed field racer +0xAAC; boosting at Lv. 5 reached 301-333 [confirmed, RAM + speedometer 319 shot].
- Release notes of v0.8.1: "Coming next" preview with 4 pictures (release assets). The GameFAQs guide is kept
  locally (docs/local/, ignored; its author forbids re-hosting); docs/notes/gear-stats.md has the numbers + credit.
- Also: Mac session moved the iOS app-ID fix into v0.8.1 (v0.8.2 dropped by the user).

## 2026-11-13 (PC, Mike, night): gold capsule respawn, dash panels, Lv. 5 at 350, menu details

- Gold 100-ring capsule: the capsule update (REL fn_1_80380 = 8188043C, a task list node per capsule) reads its kind
  from work +0x50 when a racer touches it: 0x10 goes to state 4 with a 3 s respawn timer, 0x11 (the gold one) to
  state 0, gone for the race. Found from a pickup log hook + RAM dump scan (records with +0x1A item type 12) on a
  PC Splash Canyon route [confirmed]. With the 999 cap the gold kind takes the 0x10 path with a 25 s timer (user's
  value). The user played it on PC with SR_RECORD: 5 pickups, +100 each, >= 26 s apart [confirmed, log]. Recording
  header flags are little-endian (bit 2 of byte 16 = 999 cap); a cap-100 replay took it once only [likely: the
  replay drifts from the inputs once the code path differs].
- Scripted steering to the capsule was slow: VI fields are not det steps (SR_DETCLOCK input runs on steps; the log's
  vi_field_count drifts between runs). Asking the user to drive it with SR_RECORD was much cheaper.
- Dash panels: replaying the user's recording with an object-kind log found kind 0x83E setting the racer speed to
  the panel's work +0x3C (Splash Canyon 0.926 = 200 km/h; 256 -> 200 at Lv. 5) [confirmed]. Now max(current,
  panel) with the 999 cap: 256 stays 256; a slow arrival (speed forced to 0.3) still gets 0.926 [confirmed, replay].
  No extra push (chosen overnight, ask). 0x820 (ramp?) and 0x848 (dash ring?) also change speed, untouched.
- Lv. 5 moved to 350 rings (user, after an OLED run of the 200/500 build: 500 near impossible), weaker multipliers
  picked by us (Blue Star 205/288, 225/325); PC ring pokes: Lv. 4 at 210, Lv. 5 at 360, block values exact [confirmed].
- Settings menu: every help text gets a details arrow; first version made the arrows pad stops and Down skipped
  the next option (PC shots), so they are NoNav and Y / I toggle the highlighted option's details [confirmed, shots].
- Build note: ninja did not rerun the recompiler after a patches.txt edit once; running gcrecomp.py by hand fixed it.
- SwitchWakerHD v1.3.0 read for ideas (plans-2026-11.md end). Forwarder NSP rebuilt with "Icon 3" for the OLED.

## 2026-11-14 (PC, Mike): OLED fps dips investigated; 1.25x resolution; panels +25 / Speed Shoes +80

- User: fps "fluctuating HARD" on the OLED (handheld, overclock 1785 / 614.4 / 2133 per the sys-clk overlay, never
  changed). Settings: Fit window (wide), FOV 100-120 %, 1.5x then 1.25x, Auto (keep speed), bloom 70, grading,
  sharpness 50. Five logs (log-reader + own awk): dips to 30-45 fps happen in views with 4,000-5,900 draws/frame,
  where the GPU needs 15-17 ms and the GP thread 12-24 ms; light views hold 60 [confirmed, logs].
- A/B on the same settings, Metal City: v0.8.0 (sonicriders.v080.nro) dips the same way (14 of 48 race blocks under
  55, lowest 30) [confirmed, log], so the 999-ring changes are not the cause. Runs differ by how long they spend in
  heavy views. My guess at a lost sys-clk profile was wrong (the overlay showed 614.4 the whole time; sys-clk's
  config.ini there may not be what that build uses) - do not chase clocks from that file again.
- Added 1.25x (800x660) for Switch and phones (saved as resolution=3). Panels: max(panel, own + 25 km/h); Speed
  Shoes max(200, own + 80 km/h), tested on the user's recording. The settings-menu details drop-downs were removed
  (user does not like them).
- Open: the heavy-view cost itself (GP thread draw processing + GPU) at these settings; ideas in plans-2026-11.md
  (SwitchWakerHD list: PGO, tiled cache, core 3) and a steadier Auto (30 when busy) the user declined for now.

## 2026-11-14 (PC, Mike, later): Switch heavy-view dips fixed: three swapchain images; cheaper bloom

- **"GP thread busy" was mostly waiting** [confirmed, code + logs]: it was timed around gp_feed, which includes
  render_submit / render_take_free_frame (waits for the render thread). Heavy blocks: 24.7 ms "busy" = 17.1 ms
  waiting + 7.6 ms work. Real GP work in heavy views ~6-8 ms. Now logged apart ("+ X waiting for the render
  thread"); frame_pacing's Auto check uses the work only (8b8ed8c).
- **GPU cost split on the OLED** (handheld 1785/614/2133, Fit window wide, 1.5x, bloom 70, grading, sharpness 50,
  Metal City 1P; new `SR_GPU_AB=2/3/4`, rotating per frame on the same views) [confirmed, logs]: in heavy race views
  picture options ~5 ms (bloom ~3.0, FSR ~1.25, grading ~0.9), vertex lighting ~1.3, texgen ~0.8; tiled cache on
  +0.6 ms (worse). GPU time was the same at 1.25x and 1.5x. So specialized vertex shaders are worth ~2 ms at most.
- **Bloom/FSR passes** (8b8ed8c, 049f5b9): the bloom composite and FSR's 1:1 copy ran vk_blur with no direction
  (5 reads of one texel per pixel, the composite twice at full size). Now one-pass mix (vk_bloom_mix.frag), one-read
  copies, both blur rounds as one 17-tap pass, bloom + grading in one pass (vk_bloom_grade.frag). Same picture:
  same demo-race frame in Eden with SR_DETCLOCK + SR_GPU_AB_FIX, mean difference 0.3/255 (FPS counter only)
  [confirmed]. OLED A/B: -1.0 ms GPU per frame in heavy views, -0.25 ms elsewhere [confirmed]; per-pass timestamps
  (new "[deko3d]   picture options ... GPU ms/frame" line): bright 1.5, blur 0.36, bloom + grading 1.26, FSR copy
  0.33, FSR sharpen 1.04 ms. Pass and barrier counts were not the cost; picture reads/writes are.
- **Three swapchain images** (c840558; `SR_SWAP_IMAGES=2` = the old two): heavy views at 12-15 ms of GPU still ran
  46-52 fps with two, because a frame that ended just after a retrace kept the other image on screen and the next
  picture waited a whole refresh. Same settings and race, one run each [confirmed, logs]: two images 4-5k draws
  57.3 fps avg (low 39.3), 5k+ 52.9 (low 33.4); three images 59.9 (low 59.9) and 58.9 (low 56.2); GPU peak the
  same (17-18 ms). User: "best run yet", controls felt the same [accepted, Metal City 1P at these settings only].
- Eden v0.1.1 crashed at start (host access violation, log empty) on a fragment shader with an early `return`
  (vk_blur's first one-read version); the if/else form runs [confirmed, bisect]. Rule added to the shader notes.
- Not checked: other tracks, 2-4P, docked, stock clocks with three images; Android with the shared vk_blur change
  (Android always passes color.w = 0, so the old path) [uncertain, not built]; the PC Auto mode with the new GP figure.

## 2026-11-14 (PC, Mike, evening): v0.8.2 checks; Riders Plus charge jump

- v0.8.2 checks: OLED c840558 (user): Splash Canyon Lv. 5 at 350, gold capsule after 25 s, dash panel +20-30,
  docked 60, 2P handheld/docked 60 [accepted]. Logs (log-reader): 59.9 fps; above 6k draws 55 handheld / 54 docked,
  GP thread 14-15 ms the limit; handheld 2P "game code" hitches 60-170 ms [confirmed, logs]. S23 (wireless adb,
  perf demo, 1x bloom 60 FSR 60): main and a v0.8.1 APK built from the tag give the same 1P 52 / 2P 57 fps, so the
  vk_blur change costs nothing; both below the 2026-11-10 record (57.9), the phone's state [likely]. The first
  S23 run (49 fps) used the phone's own 1.5x + sharpness 100. PC Metal City race on Auto: avg 59.9, no window
  below 58.5 [confirmed]; a first try at 30 fps came from docs saying SR_FRAMERATE=2 is Auto (it is 1; note fixed),
  and a 30 s stall in another try came while the PC was building the old APK on all cores [likely].
- Charge jump: a debug build logging every store to player 1's speed (+0xAAC, 316 sites, local only) during the
  user's PC race found fn_1_6D5D4: while racer +0xA28 bit 0x1000 is set it subtracts 0.009259 x (t/50)^2 per step
  (t = +0x9F4, max 50) down to 80 km/h and zeroes +0xAA8 [confirmed, log + disassembly]. game_levels.c delays that
  curve (hooks 8186D6AC, 8186D750). The user then found that a hold right after a boost still slowed at once:
  SR_RAMSAMPLE of the user's recording showed a charge clears the boost bit 0x400 and the decay above top speed
  (store 81815ABC, racer r24) takes over [confirmed]; it now waits too. User picked 0.75 s after playing 1 s.
  Verified by replaying the user's SR_RECORD runs: speed holds 0.73 s, then the old curve [confirmed]; accepted on
  PC. A recording fixes the option for the whole run, so the menu toggle did nothing in the test (by design).
- Option renamed "Riders Plus" (user's choice). Not done: Switch/phone play of the charge jump, CPU racers.
- Tooling notes: ninja missed a patches.txt change once (run gcrecomp.py directly after editing patches); sed -i in
  Git Bash turns CRLF files into LF (restore with sed 's/$/\r/' for files git lists as i/crlf).

## 2026-11-13 (Mac): v0.8.2 iOS zip; Riders Plus on an iPad (A16)

- iOS zip built with `make_release.py --version v0.8.2 --flavor ios --ref v0.8.2` (VERSION "v0.8.2 (81ee3358)",
  211 files); cold Mac unzip test PASS (ditto into a fresh folder, unsigned, fresh tools dir, 7/7 steps, .ipa
  built) [confirmed]; uploaded to the v0.8.2 release, the notes' last line now points at it.
- iPad (A16) over the cable, build 3ed95db (the tag only changes CHANGELOG.md), no settings file = defaults:
  `SR_TEST_MENU=3` frame shows the Gameplay tab with "Riders Plus" ticked [confirmed, SR_SCREENSHOT].
- Scripted Metal City Free Race (the PC menu route with a new card `SR_CARD=saves/test_v082.raw`, then SY=100,
  SX +-60 every 120 fields; A held 90 fields at cruise and 30 fields after a B+X boost; `SR_RAMHASH=30000
  SR_RAMHASH_BLOCKS=none SR_RAMSAMPLE=ramsample.txt` on racer 0 speed 81A0E1EC, flags 81A0E168, charge 81A0E134):
  cruise holds flat 45 steps then 130 -> 122 km/h over 30 steps (the old curve, shifted); after a boost 229 km/h
  held 45 steps, then the decay [confirmed]. Two of the boost holds lost speed earlier (193 from step ~35; 200 ->
  186 at step 15): the racer was steering into walls, not checked further [uncertain]. Not played by hand.

## 2026-11-13 (Mac, later): signatures renewed, the two older iPads moved to the standard ID

- Renewed by reinstalling (no launch): iPhone and A16 iPad (standard ID), the iPad (7th gen) and the iPad Pro (plain ID,
  a `--bundle-id net.srnative.sonicriders` build). Then the user asked to move the two iPads: iPad Pro moved over
  Wi-Fi (`install --move`) [confirmed]. the iPad (7th gen): old app uninstalled, then the install was refused ("This
  provisioning profile cannot be installed on this device", MIInstallerErrorDomain 13): Xcode's cached profile for
  `<base>.<team>` listed 4 devices, not that iPad, although the team had it registered (the plain-ID profile had 5).
- Fix: cached profile moved aside (`~/Library/Developer/Xcode/UserData/Provisioning Profiles`), app rebuilt, new
  profile with all 5 devices; installed, `ios_run.py restore` from the move's backup, card_a.raw MD5 equal, disc
  and settings back [confirmed]. Found on the way: the earlier "renewals" reused cached profiles ending 2026-11-17
  (standard) and 2026-11-14 (plain): a reinstall only renews when Xcode makes a new profile. All four devices
  reinstalled with the new one (ends 2026-11-21) [confirmed, profile date]. No device launched.
- Open: `move` uninstalls before it knows the install will be accepted; it should check the device UDID against
  the built app's embedded.mobileprovision first [to do].

  186 at step 15): the racer was steering into walls, not checked further [uncertain]. Not played by hand.

## 2026-11-14 (PC, late evening): online research; first PC-PC LAN session

- Online play is the focus until v1 (user). Research of how Dolphin, Slippi, Anchor, CoopNet, Ryujinx and
  switch-lan-play connect players: docs/notes/online-research.md (recommendation: lobby codes, hole punching,
  relay fallback, direct IP kept). Found: the Android manifest has no INTERNET permission.
- PC-PC over the LAN with the gaming laptop (test folder with two .bat files, scripted 2P route, delay 4): 20,726
  frames, all checksums equal, recordings identical, no backstops, 59-60 fps on both; the joiner was told when the
  host closed [confirmed, logs]. Details: ONLINE.md "2026-11-14".
