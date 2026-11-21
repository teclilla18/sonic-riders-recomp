# SR2Diag: native Switch 2 diagnostic

Standalone Phase 1 probe for firmware 23.0.1, kaiHEN (initial tester reports
20261117.1) launched through Seedsow and native-game hold-R takeover. This
application does not read or link any game code, assets, disc image or save.
It makes no clock/power-profile changes and has no network connection test.

## Build

Python 3.12+, devkitA64 GCC and switch-tools are required. The isolated SDK
archive is downloaded and SHA-256 checked against `dependencies.json`.
The system libnx/portlibs installation is never overwritten.

```
python diagnostics/switch2/build.py --devkitpro <devkitpro>
```

Output: `<repo>/build/switch2-diag/sr2diag.nro`. The same NRO and its manifest
are staged under `<repo>/build/switch2-diag/sd-card/switch/SR2Diag/`.
`SR2Diag-Switch2-0.1.0.zip` packages that SD layout and dependency notices.
`build-info.json` records source hashes, compiler, SDK/component revisions,
compiler flags, tool and library hashes. `sr2diag.nro.sha256` hashes the result.
No game configure.py, generated code or existing platform build is involved.

## Install and test

1. Copy the staged `switch/SR2Diag` directory to `<sd>/switch/SR2Diag`.
2. Use the same native Switch 2 game + hold-R route that runs Dusklight.
   Launch **SR2Diag - Switch 2 Diagnostics**; use normal console power settings.
3. The menu shows PENDING/PASS/FAIL/SKIPPED. D-pad selects; **A** runs one stage,
   **X** skips it, **+** requests exit. Probes do not autorun. Run in menu order.
4. Input stage: press A/B/X/Y, L/R and all D-pad directions; move both sticks
   within 30 seconds. Audio: listen for a quiet left low tone then right higher
   tone; Y confirms, B reports failure, X skips confirmation.
5. Run each guest-memory strategy separately. An unavailable process handle or
   code-map SVC hint is SKIPPED. A failed strategy is not necessarily fatal if
   the other works; any cleanup failure disables further probes.
6. Vulkan temporarily replaces the menu with RED, GREEN, BLUE (60 presents per
   color), then restores the results. Confirm Y/B/X. Do not dock/undock, sleep
   or press HOME during this initial diagnostic. Repeat later in the other mode.
7. Press **+**, confirm hbmenu returns, relaunch once, then launch OunceShell.
   Exit-hook success is logged separately from this external return/relaunch check.
8. Return both run/driver logs, build-info.json and your observed results.
   State the native host title, exact kaiHEN version, docked/handheld mode and
   known clocks (or "stock policy; clocks not measured").

## Persistent logs and failed stages

`<sd>/switch/SR2Diag/run-000001.log` and `driver-000001.log` retain each run;
the next launch uses a new number. `latest.txt` identifies the latest run.
Every application checkpoint is flushed and fsynced before continuing. The
driver writes unbuffered stderr to its separate file. A failure to open the
application log disables the probes. The last BEGIN checkpoint helps isolate
a crash or hang. Driver-internal calls may still block despite explicit fence,
image-acquire, audio and thread wait timeouts. Do not remove the SD while running.

If a stage stops responding, recover through your normal console procedure,
retain its logs, and skip that stage on the next launch to isolate the others.
If memory/thread/GPU cleanup reports failure, stop testing and restart the
homebrew session rather than run more probes in that process.

## What a PASS proves

Services check process information, firmware query, sockets init/exit, embedded
RomFS and SD write/flush/seek/read. Timing checks CLOCK_MONOTONIC against the
SDK physical counter and its 31.25 MHz conversion; it does not execute
`cntvct_el0`. Workers test allocation, TLS isolation, actual allowed core
affinity and join/close. Guest mapping reserves 512 MiB + 64 KiB of **virtual**
space, backing 28 MiB + 64 KiB, page-tests both ranges and unmaps them.

Vulkan checks instance/device/surface/swapchain, native presentation, transfer
clears, synchronization and cleanup. It reports features/formats/limits for
future integration. It does **not** validate shaders, game pipelines, 3D GX
rendering, long-term performance, multiplayer, docking or sleep. Audio transport
and visible Vulkan output need the separate tester confirmations. Pending tests
become SKIPPED on exit. The shutdown row describes owned-resource cleanup;
only the tester can confirm actual return to hbmenu and another app. The final
shutdown result is written after the text display closes, so check it in the log.

The first application log checkpoint follows libounce's default service
bootstrap. A loader/bootstrap failure before that hook cannot be captured by
this application; report any loader error and whether a new log was created.

## References

- ounce-devkit `devkit-20261117`, components in dependencies.json; native
  Vulkan presentation follows its MIT `examples/vulkan-clear` integration.
- Dusklight `v2.0.3-switch2-1` (ad781a8fcd25e17597dcf906217e2d4079d2aa93),
  Aurora dc1a528e81c8972443721ae47d324773001bc0ca: startup/exit, permitted
  core masks, native windows and memory diagnostics informed this independent
  implementation. No Dusklight/Aurora/Dawn/SDL code or libraries are linked.

Source is MIT; dependency licenses remain in THIRD_PARTY_NOTICES.md and the SDK.
