# Debug variables (SR_*) and recompiler switches (RECOMP_*)

**Reference.** Moved from docs/REFERENCE.md on 2026-11-03 (content unchanged). Index: [REFERENCE.md](../REFERENCE.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## Debug variables (SR_*): full list

Set them in the environment on PC; on the Switch put `KEY=VALUE` lines in `env.txt` in the game's SD folder (lines up
to 8 KB). "PC" = desktop build only. Details: grep the variable in this file and in HISTORY.md.

**Running and capturing**
- `SR_MINIMIZED=1` (PC): window stays in the background. Use for every test run (the user may be using the PC).
- `SR_HEADLESS=1` (PC): no window. `SR_NOVSYNC=1` (PC): present without vsync.
- `SR_NOAUDIO=1`: silent host output. `SR_AUDIO=0`: mixer off. `SR_WAVOUT=<file>`: record the mixed output.
- `SR_SCREENSHOT=<frame>[,...]` / `list` (PC): `build/shot_<frame>.png`, up to 512 frames. `SR_SHOTSTEP`: capture by frame end (from 1).
- `SR_SCREEN=0..3` (4:3, 16:9, stretched, fit window), `SR_FOV=80..130`, `SR_HUDSTRETCH=0/1`: screen settings for one run, not saved (notes/ultrawide-fov.md). `SR_WINDOW=WxH` (PC): the window's first size (wide captures, e.g. 3440x1440).
- `SR_SHOTS=1` or `SR_SHOTS=a,b,c` (Switch): `shot_<frame>.bmp` at 300 … 7200, or those frames. Off by default.
- Android only: `SR_TOUCH=0` hides the touch controls (picture compares); `SR_VK_TINYSCISSOR=1` (1-pixel scissor: GPU cost without pixels) and `SR_VK_FULLAREA=1` (whole-EFB render area) are GPU cost tests; `SR_VK_NOPUSH=1` no push descriptors. Device-lost tools (2026-11-06): `SR_VK_FAKELOST=<frame>[,...]` acts as if the device were lost after those frames (tests the rebuild); `SR_VK_DRAWS=lo-hi` sends only those draws of each frame; `SR_VK_ONEPIPE=1` keeps each pass's first pipeline; `SR_VK_FORCE=cull,depth,dtest,dwrite,dalways,blend` fixes those pipeline fields (wrong picture); `SR_VK_SWAPIMAGES=<n>` swapchain length; `SR_VK_PRETRANSFORM=1` renders pre-rotated to the panel (swapchain transform = current).
- `SR_INPUT="1500:START,1510:-START,7300:2.START,4000:SX=50,..."`: scripted buttons/stick at VI field counts, port prefix `N.`; up to 1024 steps; PC and Switch (input_script.c).
- `SR_PADS=N`: connect ports 1..N without gamepads (PC). `SR_CARD=<path>`: memory card image (both targets).
- `SR_TEST_MENU=1` / `<tab>` / `mods`: start with the settings menu open on that tab.
- `SR_FRAMERATE=0/1/2`: 60 fps / 30 fps / Auto for tests. `SR_TEST_GAME_LOAD_MS=DRAW[:LOGIC]`: busy-wait on the game thread to imitate a slow console.
- `SR_HANDHELD_GPU=0` (Switch): don't apply the 384 MHz handheld GPU profile.

**Hangs, crashes, timing**
- `SR_WATCHDOG=<s>`: function trace + device accesses + guest threads dumped when time runs out. `SR_WATCHDOG_NOTRACE=1`: dump without the trace. On the Switch its exit ends in a system error screen.
- `SR_LOGSYNC=1`: flush every log line. `SR_DVDLOG=1`: every disc read by file name, completions, ARAM DMAs.
- `SR_DVD_ATEND=1`: old disc model (data only at completion; test). `SR_DVD_MBPS=<n>`: modelled disc speed.
- `SR_WATCH=ADDR,VALUE` / `ADDR,~VALUE` / `ADDR,*[,A2,A3,A4]` (PC): watch guest memory, sample host threads.
- `SR_PEEK`, `SR_POKE` (per present, one byte), `SR_POKEAT=<frame>:<addr>:<hex>[,...]` (deterministic), `SR_DUMPRAM` (PC).
- `SR_GPTL=<field>`: timeline of game idle / PE finish / tokens / retraces. `SR_GPSLOW=<factor>`: slower GP thread (imitates the Switch).
- `SR_GX_SYNC=1`: no GP thread (old path). `SR_DSP_SYNC=1`: AX mixing on the game thread (old path). `SR_DECODE_HELPER=0`: no vertex-decode helper thread.
- `SR_DECODECHECK=1`: run old and new vertex decoders and compare. `SR_NOTLUTSNAP=1`: no palette snapshot at issue time.
- `SR_AXLOG=1`: dump AX parameter blocks every 400 lists. `SR_ARAMDUMP=<file>`: dump ARAM after 3000 lists.

**Profiling and performance**
- `SR_PROFILE=1`: PC sampling profiler (`tools/profile_report.py`); Switch `[profile]` lines every 10 s ("last function entered": a caller's work after a call is charged to the callee). `SR_PROFILE_WINDOW=FROM,TO`, `SR_PROFILE_OUT=<path>` (PC).
- Vita (env.txt): `SR_PROFILE=1` as on the Switch (core 1, 40 entries + PROF tag shares; `SR_PROFILE_SECS=N` report period); `SR_GX_TIMERS=1` turns on the per-draw `[perf]` timers (off there: a clock read costs ~0.7 us); `SR_VITA_UNCACHED=0` (frame vertex arrays on the heap, not uncached memory), `SR_VITA_CPU`, `SR_VITA_STALLCORE` (docs/notes/vita-research.md).
- `SR_PERFTEST=1` (+ `SR_PERFTEST_OUT=<path>`): the standard performance test, see docs/PERF.md.
- `SR_GPU_AB=1` (Switch): GPU timestamps and shader counters, alternating XF paths per frame.

**Graphics debugging**
- `SR_GXDEBUG=<frame>` (+ `SR_GXDEBUG_ALL`): that frame's viewport/scissor/copies/draws.
- `SR_TEVDEBUG=<view>|cycle|cycle:a,b,...` (PC): debug views (1 lit colour 0, 2 stage-1 texture, 3 stage 0 only, 6 stage-1 coordinates, 7 old specular direction, 8 lit colour 1, 9 no fog, 10 fog amount, 11 fog inputs). `SR_DK_DEBUG_TEV=<view>`: build-time, bakes a view into the deko3d shaders (tools/embed_shaders.py).
- `SR_SKIPSAME=<frame>,<n>[,<first>]` (PC): redraw a frame without each block of n draws (`build/same_<frame>_<k>.png`). `SR_SKIPSCAN=<frame>,<step>` (PC): related scan mode in render_gl.c.
- `SR_DFF=<frame>[,<count>]`: record frames as a Dolphin FIFO log (record with `SR_GX_SYNC=1`).
- `SR_DUMPTEX`, `SR_TEXWATCH=<hex addr>`, `SR_GLTEXDUMP=<hex addr>` (PC): textures, palette slots, the bound GL texture.
- `SR_NOALPHABLEND`, `SR_SHOWALPHA` (PC): alpha investigations. `SR_NOSPEC=1`: ubershader only (no specialized shaders).
- `SR_SHADER_KEYS=<file>`: log new specialized-shader keys (Switch writes `shader_keys.txt` on cache misses).

**Determinism and online play** (details in docs/ONLINE.md)
- `SR_DETCLOCK=1`: deterministic mode. `SR_DETTRACE=1`: log every interrupt/disc command with frame and location.
- `SR_RAMHASH`, `SR_RAMHASH_BLOCKS=<from>-<to>|none`, `SR_RAMHASH_SUB=<block>`, `SR_RAMHASH_REF=<log>` (+ `SR_RAMHASH_REF_DUMPS`), `SR_RAMHASH_KEY=<addr>` (adds e.g. the scene frame 8021BB64), `SR_RAMDUMPAT`, `SR_RAMDUMPKEY[_AFTER]`.
- `SR_RECORD=<file>`, `SR_REPLAY=<file>`, `SR_NET=host:[<addr>:]<port>` / `join:<addr>:<port>`, `SR_NET_DELAY`, `SR_NET_TIMEOUT`, `SR_NET_TEST_DESYNC=<frame>`, `SR_ONLINE_SAVE=1`, `SR_VIEW=<1|2>`.

## Recompiler switches (RECOMP_*)

Environment variables read by `tools/recomp/gcrecomp.py` when generating code (all on by default unless noted):
- `RECOMP_CALLEE_SAVED=0`: don't keep callee-saved registers in locals across calls.
- `RECOMP_SINGLE=0`: no single-precision tracking (singleflow.py); `RECOMP_SINGLE_CHECK=1`: checking build.
- `RECOMP_STACKRAM=0`: r1/r13-based accesses keep the device test.
- `RECOMP_HOT=0`: don't emit `config/GXEE8P/hot_functions.txt` functions first.
- `RECOMP_SUMMARY=0`: no register summaries at calls; `RECOMP_SUMMARY_CHECK=1`: checking build.
- `RECOMP_FLOAT=1` (off by default) / `RECOMP_FLOAT_CHECK=1`: single-precision ops computed in float (no gain).
- `RECOMP_PROFEXACT=1` (profiling builds only): after each call the caller writes its own address back to
  `g_ppc_cur_fn`, so `SR_PROFILE` charges a caller's code to the caller (default: to the last function entered).
  One store per call. Recompile into its own folder (`gcrecomp.py config/GXEE8P baserom <dir>`).
- `configure.py --no-base-reg`: Switch without the x28 guest-memory base register.
- `configure.py --vita-base 0x90000000`: Vita, guest window reserved at that fixed address and used as a constant
  (no base load per access); the program stops at start-up if the window lands elsewhere.
- After editing `config/GXEE8P/patches.txt`, run `python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen`
  if ninja says there is nothing to do [uncertain].
