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
