# Native Switch 2 experimental target

Work locally on `feature/switch2-native`. Do not push, merge or publish it.
This target is separate from the original libnx/deko3d Switch target and uses
the pinned ounce-devkit20261117/libounce/OunceVK validated by SR2Diag.

## Phase 1 hardware evidence (2026-11-17)

SR2Diag48b77e581423: physical Switch 2, firmware23.0.1, Seedsow/kaiHEN reported
20261117.1. Log operation mode0 means handheld; clocks were not recorded.
This evidence is functional testing, not a performance measurement.

- **Tested [confirmed]:** startup, SD/RomFS/service queries, timing, workers/TLS,
  controllers, audible audio, stack SVC mapping, Vulkan clear/presentation and
  tester-confirmed red/green/blue output. No diagnostic failure in returned logs.
- Timing reports31.25MHz; process mask0x3f permits cores0 through5. All worker
  join/close, audio stop/flush and guest unmap/reservation release calls succeed.
- Vulkan1.4 reports dynamic rendering and synchronization2. Five complete
  180-presentation runs finish device idle/destruction and restore the console.
  Shader/game-pipeline compatibility was not tested by the clear diagnostic.
- Code mapping skips automatically. The own-process handle is valid, but one
  or more hints for0x77(map),0x78(unmap),0x73(permission) are absent. The diagnostic
  did not log individual bits, so it cannot identify the missing one.
- Visual-result-only menu rows can overwrite a previous confirmation with
  SKIPPED when selected with A. Those rows do not negate actual visual PASS
  confirmations. The tester reported no intentional skips.
- Both runs log explicit resource cleanup complete. Run2 confirms a relaunch.
  Neither records EXIT_HOOK; stdio is likely closed by newlib before userAppExit.
  This is an instrumentation gap, not evidence of a failed Vulkan/memory cleanup.
  Final hbmenu/OunceShell behavior was not independently recorded in those logs.

## Why the passing mapping is sufficient for the core's memory contract

`runtime.h`: guest backing0x01c00000 bytes (28MiB), locked-cache physical
offset0x20000000, logical locked cache0x4000 bytes. The diagnostic reserves
0x20010000 bytes of virtual address space and backs28MiB plus64KiB, matching
the original Switch fallback exactly. Both mapped ranges report RW permissions,
are page-tested and are unmapped before their heap backing is freed.

`ppc.h`: physical address is `ea & 0x3fffffff`; cached/uncached RAM aliases
fold onto the same backing. MMIO is routed to device handlers. Recompiled
functions execute in host executable text; guest RAM contains data and does
not require executable permissions. The process-code mapping path is an
alternative placement mechanism, not a JIT requirement. Core integration can
therefore use the tested stack mapping without depending on unavailable hints.
This proves the layout contract, not a completed game boot or race.

## Phase 2 implementation

`configure.py --target switch2` dispatches to `tools/switch2_build.py` without
changing the build graphs of host, Switch, Android or iOS. The new graph uses
the Phase1 SDK after verifying its archive and linked inputs, emits output
under `build/switch2/`, and compiles shared shaders as SPIR-V.

```
python configure.py --target switch2 --devkitpro <devkitpro> --ounce-sdk <sdk> --glslc <glslc> --imgui <imgui>
ninja -j4
```

Use native Python and the native Ninja executable on Windows. The SDK root
defaults to `build/switch2-diag/deps`. No global SDK/library replacement occurs.
Own-disc `baserom/main.dol`, `baserom/_Main.rel` and local dtk symbol lists are
required; generated game code, symbol files and NROs remain ignored and private.

The native platform uses permitted core discovery, the verified physical
counter, and direct audout/input APIs. It shares the Vulkan rendering logic
with Android/iOS while using native NWindow/VI presentation and no SDL/touch
dependency. Initial integration uses the ordinary memory-base path rather
than enabling an untested fixed-x28 optimization. Switch1 APM settings are
neither requested nor shown in the native target's settings menu.

The render loop stays on main. Managed workers use cooperative shutdown at
guest polling points, sleeps and condition-variable waits; join completes
before renderer/audio/guest mappings are destroyed. Driver-owned condition
variables retain their original behavior. A timed-out worker prevents returning
an active thread to hbloader. Actual integrated shutdown still needs hardware
validation; Phase1 results cannot validate newly integrated workers/pipelines.

Filesystem-level lifecycle markers avoid the stdio exit-hook ordering issue.
Runtime data is isolated under `/switch/sonicriders-switch2/`: own ISO/GCM,
env.txt, settings.ini, saves/, mods/, sonicriders.log and sonicriders.prev.log.
`lifecycle-NNNNNN.log` records checkpoints and EXIT_HOOK through an FsFile,
which remains open until userAppExit. Returning to hbmenu and then OunceShell
is a separate tester confirmation.

## Initial hardware procedure

- Copy only the experimental NRO to `/switch/sonicriders-switch2/`; copy your
  own vanilla USA GXEE8P disc image there, or set SR_DISC in env.txt to its path.
- Leave texture packs, enhancements and networking off for the initial boot.
  `SR_SWITCH2_FRAMES=300` requests a bounded exit after300 presented frames;
  it cannot time out a driver/core hang before frames arrive.
- Launch through the already-tested native-game hold-R flow. Look for intro,
  title/menu, audio and controller response. Do not assume 60fps or resolution.
- Use the settings menu's Quit Game, confirm hbmenu returns, relaunch and try
  OunceShell. Return runtime/previous/lifecycle logs and report mode/known clocks.
- Then remove the frame bound and test one complete race. Docking, sleep,
  multiplayer, texture packs and long-run stability remain later validation.

Dependency references: ounce-devkit b11c29cba8d476f193bdc78ac98f344c167e541c;
Dusklight v2.0.3-switch2-1/ad781a8fcd25e17597dcf906217e2d4079d2aa93 and its
Aurora dc1a528e81c8972443721ae47d324773001bc0ca informed platform patterns.
No Dusklight/Aurora/Dawn game code or dependencies are linked into this port.
