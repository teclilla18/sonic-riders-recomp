# Experimental Switch 2 state (2026-11-17)

This records feature/switch2-native. The public main worktree and its roadmap
remain untouched. Do not push, merge, publish or release this branch.

## Current objective

Phase 1 is hardware-tested. Phase 2 adds the isolated native build target and
existing recompilation core; next is the user's integrated first-boot test.
See docs/notes/switch2.md for architecture, commands and validation limits.

## Implemented

- SR2Diag: staged native NRO with flushed per-run SD logs, system services,
  timing, worker/TLS/affinity, controller, audio, sparse memory and Vulkan tests.
- configure.py --target switch2; tools/switch2_build.py; isolated output under
  build/switch2 using checksum-verified ounce-devkit20261117, devkitA64 and glslc.
- Existing guest core and shared Vulkan rendering, native NWindow presentation,
  existing audout resampler and HID input. No SDL, deko3d or Switch1 power profiles
  in the new target. Guest-base register optimization is initially disabled.
- Separate SD data under /switch/sonicriders-switch2/; worker stop/join precedes
  GPU/audio/memory destruction. FsFile lifecycle log survives stdio teardown.
- SR_SWITCH2_FRAMES=N provides a bounded presented-frame exit for bring-up.

## Tested evidence

- [confirmed] SR2Diag48b77e581423 on physical Switch2, firmware23.0.1,
  Seedsow/kaiHEN reported20261117.1, handheld(mode0), clocks not recorded.
  Startup, services/SD/RomFS, timing, workers/TLS/all permitted cores, controls,
  audible audio, stack mapping and tester-confirmed Vulkan colors PASS.
- [confirmed] Exact guest layout: reserve0x20010000 bytes VA, back28MiB and64KiB
  at offsets0 and0x20000000, RW, page-test and unmap. Static recompilation executes
  host text and needs no executable guest RAM. Stack mapping meets this contract.
- [confirmed] Process handle valid; code-map test automatically SKIPPED because
  at least one required syscall hint was absent. Which bit was not logged.
  It is an optional mapping alternative, not a core requirement.
- [confirmed] Diagnostic workers, audio, mappings, Vulkan objects and console
  explicitly clean up; second run reaches main. Final EXIT_HOOK is absent:
  [likely] stdio closed before the FILE logger, not a proved resource failure.
  Native integration now logs lifecycle through FsFile; hardware check pending.
- [confirmed] Integrated native compilation/link/NRO packaging succeeds locally.
  Original Switch preprocessing unchanged for entry/scheduler/menu. Android
  renderer syntax passes. These are build checks, not runtime/gameplay tests.

## Still unproven

Integrated Switch2 boot, GX shaders, intro/menu/race correctness, sound pacing,
cooperative shutdown/hbmenu return, relaunch, memory use and performance.
No accepted or released native Switch2 game build. No FPS/resolution promise.
Docking/sleep, multiplayer, network sessions, texture packs and long sessions
wait until basic correctness. Existing platforms were not fully replay-tested.

## Next hardware checks

- Use the already-tested native-game hold-R route and normal power policy.
- Install build/switch2/sonicriders-switch2.nro in /switch/sonicriders-switch2/.
  Supply own vanilla USA GXEE8P ISO/GCM there; no game data is included separately.
- First run with SR_SWITCH2_FRAMES=300; look for intro/title/menu, sound and input.
- Confirm automatic exit/hbmenu, relaunch, then OunceShell. Check lifecycle log
  for PRE_LIBC_SHUTDOWN_COMPLETE and EXIT_HOOK; return both runtime/lifecycle logs.
- Then remove frame bound and play a complete race. State mode and known clocks
  or stock policy/clocks unmeasured with every hardware report.
