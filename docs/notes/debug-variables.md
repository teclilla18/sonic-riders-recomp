# Debug variables (SR_*) and recompiler switches (RECOMP_*)

**Reference.** Moved from docs/REFERENCE.md on 2026-11-03 (content unchanged). Index: [REFERENCE.md](../REFERENCE.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## Debug variables (SR_*): full list

Set them in the environment on PC; on the Switch put `KEY=VALUE` lines in `env.txt` in the game's SD folder (lines up
to 8 KB). "PC" = desktop build only. Details: grep the variable in this file and in HISTORY.md.

**Running and capturing**
- `SR_MINIMIZED=1` (PC): window stays in the background. Use for every test run (the user may be using the PC).
- `SR_FPS=<45|120|144|180|240|-1>` (PC): picture rate with in-between pictures for this run (setting `pc_fps`,
  -1 = unlimited; 30/60 are `framerate`/0). Log: `[gl] in-between pictures: target, measured /s, draws paired`.
- `SR_SCALE=<1-4>` / `SR_MSAA=<1|2|4|8>` (PC): internal resolution and anti-aliasing for this run (settings
  `pc_scale` / `msaa`, F1 > Display); the log's `[gl] EFB` line shows what is in use.
- `SR_SHARPNESS=<0-100>` (PC, Android): FSR 1 sharpening for this run (setting `sharpness`; above 0 also EASU upscaling
  when the window is bigger than the picture). Log line `[gl] FSR 1 (EASU + RCAS) on the presented picture` / `[vk] FSR 1 ...`.
- `SR_BLOOM=<0-100>` / `SR_GRADE=<brightness>,<contrast>,<saturation>,<warmth>` (PC, Android; read in settings.c): bloom
  and colour grading for this run (settings `bloom`, `grade_*`; neutral = `0,100,100,0`). Android logs `[vk] picture options`.
- `SR_RES=<0-2>` (Switch, Android): resolution 1x / 1.5x / 2x for this run (setting `resolution`).
- `SR_CHARLIGHT=1` (PC): per-pixel lighting for this run (setting `char_light`).
- `SR_SCALE_AT=<frame>:<scale>[:<msaa>]` (PC): change resolution / MSAA at that presented frame, as the F1 menu does.
- `SR_HEADLESS=1` (PC): no window. `SR_NOVSYNC=1` (PC): present without vsync.
- Launcher: `SR_LAUNCHER_FAKE_VERSION=<v>` (pretend to be that version), `SR_LAUNCHER_UPDATE_TEST=1` (download and
  install the newest release, build\update_test.txt), `SR_INSTALLER=switch|android` (installer mode); notes/launcher.md.
- `SR_NOUPDATECHECK=1` (PC): no update check at start-up (also off with `update_check=0` in settings.ini and in
  `SR_INPUT` / `SR_PERFTEST` runs); `SR_UPDATECHECK=1`: check even in those runs (update_check.c, update_core.h).
- `SR_NOAUDIO=1`: silent host output. `SR_AUDIO=0`: mixer off. `SR_WAVOUT=<file>`: record the mixed output.
- `SR_SCREENSHOT=<frame>[,...]` / `list` (PC): `build/shot_<frame>.png`, up to 512 frames. `SR_SHOTSTEP`: capture by frame end (from 1).
- `SR_SCREEN=0..3` (4:3, 16:9, 2 = 16:9 (old stretched mode, removed), fit window); `SR_ANCHORLOG=<frame>`: that frame's 2D draws and their wide-screen anchors (notes/ultrawide-fov.md), `SR_FOV=80..130`, `SR_HUDSTRETCH=0/1`: screen settings for one run, not saved (notes/ultrawide-fov.md). `SR_WINDOW=WxH` (PC): the window's first size (wide captures, e.g. 3440x1440).
- `SR_SHOTS=1` or `SR_SHOTS=a,b,c` (Switch): `shot_<frame>.bmp` at 300 … 7200, or those frames. Off by default.
- Android only: `SR_TOUCH=0` hides the touch controls (picture compares); `SR_VK_TINYSCISSOR=1` (1-pixel scissor: GPU cost without pixels) and `SR_VK_FULLAREA=1` (whole-EFB render area) are GPU cost tests; `SR_VK_NOPUSH=1` no push descriptors. Device-lost tools (2026-11-06): `SR_VK_FAKELOST=<frame>[,...]` acts as if the device were lost after those frames (tests the rebuild); `SR_VK_DRAWS=lo-hi` sends only those draws of each frame; `SR_VK_ONEPIPE=1` keeps each pass's first pipeline; `SR_VK_FORCE=cull,depth,dtest,dwrite,dalways,blend` fixes those pipeline fields (wrong picture); `SR_VK_SWAPIMAGES=<n>` swapchain length; `SR_VK_PRETRANSFORM=1` renders pre-rotated to the panel (swapchain transform = current).
- `SR_INPUT="1500:START,1510:-START,7300:2.START,4000:SX=50,..."`: scripted buttons/stick at VI field counts, port prefix `N.`; up to 1024 steps; PC and Switch (input_script.c). Prefix `M.` = the settings menu's buttons
  (MENU UP DOWN LEFT RIGHT ACCEPT BACK TABL TABR; MENU held 0.5 s opens it), e.g. `600:M.MENU,660:-M.MENU`.
- `SR_BUGREPORT=1` (PC/Linux): write the menu's bug-report zip at start-up (test).
- `SR_PADS=N`: connect ports 1..N without gamepads (PC). `SR_CARD=<path>`: memory card image (both targets).
- `SR_TEST_MENU=1` / `<tab>` / `mods`: start with the settings menu open on that tab.
- `SR_FRAMERATE=0/1/2`: 60 fps / Auto / 30 fps for tests (settings.c). `SR_TEST_GAME_LOAD_MS=DRAW[:LOGIC]`: busy-wait on the game thread to imitate a slow console.
- `SR_HANDHELD_GPU=0` (Switch): don't apply the 384 MHz handheld GPU profile.

**Hangs, crashes, timing**
- `SR_WATCHDOG=<s>`: function trace + device accesses + guest threads dumped when time runs out. `SR_WATCHDOG_NOTRACE=1`: dump without the trace. On the Switch its exit ends in a system error screen.
- `SR_LOGSYNC=1`: flush every log line. `SR_DVDLOG=1`: every disc read by file name, completions, ARAM DMAs.
- `SR_DVD_ATEND=1`: old disc model (data only at completion; test). `SR_DVD_MBPS=<n>`: modelled disc speed.
- `SR_TRACE_MIN=<hex>` (PC, with `SR_WATCHDOG`): record only functions at or above that address (81800000 = the game's REL code) and dump the whole 4096-entry trace.
- `SR_WATCH=ADDR,VALUE` / `ADDR,~VALUE` / `ADDR,*[,A2,A3,A4]` (PC): watch guest memory, sample host threads.
- `SR_PEEK`, `SR_POKE` (per present, one byte), `SR_POKEAT=<frame>:<addr>:<hex>[,...]` (deterministic), `SR_DUMPRAM` (PC).
- `SR_GPTL=<field>`: timeline of game idle / PE finish / tokens / retraces. `SR_GPSLOW=<factor>`: slower GP thread (imitates the Switch).
- `SR_GX_SYNC=1`: no GP thread (old path). `SR_DSP_SYNC=1`: AX mixing on the game thread (old path). `SR_DECODE_HELPER=0`: no vertex-decode helper thread.
- `SR_DECODECHECK=1`: run old and new vertex decoders and compare. `SR_NOTLUTSNAP=1`: no palette snapshot at issue time.
- `SR_AXLOG=1`: dump AX parameter blocks every 400 lists. `SR_ARAMDUMP=<file>`: dump ARAM after 3000 lists.

**Profiling and performance**
- `SR_PROFILE=1`: PC sampling profiler (`tools/profile_report.py`); Switch `[profile]` lines every 10 s ("last function entered": a caller's work after a call is charged to the callee). `SR_PROFILE_WINDOW=FROM,TO`, `SR_PROFILE_OUT=<path>` (PC).
- `SR_PERFTEST=1` (+ `SR_PERFTEST_OUT=<path>`): the standard performance test, see docs/PERF.md.
- Android: `SR_NOPIN=1` (no fast-core placement / nice values), `SR_NOHINT=1` (no ADPF performance hint session).
- `SR_DET_SLOWREAD=<ms>` (SR_DETCLOCK, testing): every disc read takes that long, like a slow SD card.
- `SR_GPU_AB=1` (Switch): GPU timestamps and shader counters, alternating XF paths per frame. `=2`: per frame normal /
  no bloom / no grading / no FSR / no picture options; `=3`: normal / vertex shader without lighting / without texgen /
  tiled cache on / no picture options; `=4`: picture-option passes new vs old (same picture). `[gpu-ab]` lines every
  600 frames. `SR_GPU_AB_FIX=<mode>`: that mode only (picture comparisons, with SR_DETCLOCK).
- `SR_SWAP_IMAGES=2` (Switch): two swapchain images as before 2026-11-14 (default three).

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
- `SR_RAMSAMPLE=<file>` (needs `SR_RAMHASH=<steps>`): hex addresses, one per line (up to 4096); logs `[ramsample] step N addr old new` on every change. Use it to keep only the candidates of a RAM-dump diff that never flip (found `GAME_RACE_PHASE`, 2026-11-07).
- `SR_ONLINE_RULES=1` (the online rules offline: menus by player 1, no pause in races; docs/ONLINE.md "Online rules").
- `SR_NET_NAME=<name>`, `SR_NET_PEER=<name>` (set by the launcher's lobby: this and the other player's names, for the banners and the log).
- `SR_OWNVIEW=0|1|2` (online own view: half picture / full screen with more above and below, default / zoomed), `SR_OVLOG=<frame>` (that frame's own-view 2D moves); docs/ONLINE.md "Own view".
- `SR_RECORD=<file>`, `SR_REPLAY=<file>`, `SR_NET=host:[<addr>:]<port>` / `join:<addr>:<port>`, `SR_NET_DELAY`, `SR_NET_TIMEOUT`, `SR_NET_TEST_DESYNC=<frame>`, `SR_ONLINE_SAVE=1`, `SR_VIEW=<1|2>`.

## Recompiler switches (RECOMP_*)

Environment variables read by `tools/recomp/gcrecomp.py` when generating code (all on by default unless noted):
- `RECOMP_CALLEE_SAVED=0`: don't keep callee-saved registers in locals across calls.
- `RECOMP_SINGLE=0`: no single-precision tracking (singleflow.py); `RECOMP_SINGLE_CHECK=1`: checking build.
- `RECOMP_STACKRAM=0`: r1/r13-based accesses keep the device test.
- `RECOMP_HOT=0`: don't emit `config/GXEE8P/hot_functions.txt` functions first.
- `RECOMP_SUMMARY=0`: no register summaries at calls; `RECOMP_SUMMARY_CHECK=1`: checking build.
- `RECOMP_FLOAT=1` (off by default) / `RECOMP_FLOAT_CHECK=1`: single-precision ops computed in float (no gain).
- `configure.py --no-base-reg`: Switch without the x28 guest-memory base register.
- After editing `config/GXEE8P/patches.txt`, run `python tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen`
  if ninja says there is nothing to do [uncertain].
