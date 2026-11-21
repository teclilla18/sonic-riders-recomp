# Online Beta 1: LAN lobby and deterministic disc read-ahead

Status: **implemented**, partly **tested**, not accepted or released. Candidate branches are separate from main.

## Startup and UI

`main.c` starts the renderer, then `online_boot_wait`, before loading/executing the guest and choosing a card.
`--online`, `online`, `SR_ONLINE=1`, or a Switch NRO name containing `online` opens the lobby. A dedicated build
may define `SR_ONLINE_DEFAULT`, for loaders without launch arguments. Returning offline never sets `SR_NET`.
Successful lobby start publishes the network specification/names with an atomic handoff, then `netplay_init`
chooses deterministic timing and the generated online save before guest boot.

`menu.cpp` owns/polls the lobby on the render thread. The game thread submits presentation-only `GXC_PRESENT`
frames while waiting. No guest frames are skipped. `online_rules` removes the opaque, silent loading overlay at
the first Mode Select (screen 0x0F). Names are explicitly chosen and saved as `online_name` in settings.ini.
Switch uses swkbd; Android/desktop use SDL text input. The native Switch 2 candidate uses a controller keyboard.

Switch relaunch uses libnx `envSetNextLoad`. Android uses a non-exported foreground helper activity in another
process to replace native SDL state. An alarm-based first implementation failed on the test phone; it was replaced.
The native Switch 2 candidate starts in its dedicated Online application; relaunch support is not assumed.

## LAN wire format and compatibility

`lan_lobby.c` implements the launcher's SRLB version 1 UDP packets: ANNOUNCE, JOIN, LIST, REFUSE, PING, LEAVE,
START. Host port 7778, discovery port 7779, game port 7777. FIND (type 8) adds a direct announcement reply,
including for two copies on one PC. Announcements repeat each second; stale rooms expire after four seconds,
members after six. START is repeated for 1.5 seconds. Datagram parsing and work per poll are bounded.

The u64 lobby fingerprint is now `NET_COMPAT`, shared with the launcher in `runtime/src/net_compat.h`.
Game protocol 5 adds that number to HELLO/WELCOME; both build strings are logged for diagnosis.
**Bump NET_COMPAT** for changes affecting guest memory or deterministic execution: recompiler, HLE/patches,
gameplay. Keep the rolling full-memory checksum coverage and the host's synced gameplay/camera settings.
Presentation-only platform differences need no compatibility bump. There is no ANYBUILD bypass in this branch.

## Disc cache

`disc_cache.c` serves deterministic DI reads through two 256 KiB windows. One worker reads a separate, read-only
disc handle, never the guest buffer. Speculative reads stay within one FST file; virtual mod files bypass the
cache. A miss uses the original synchronous read and never waits for speculation. `di.c` still copies the exact
bytes and completes at the existing deterministic point. Ordinary offline DVD timing is unchanged.
The original Switch worker uses core 2: attempting core 3 failed its allowed-core mask on hardware.

`SR_DISC_READAHEAD=0` disables the cache for comparisons. The first hit and totals every 600 reads are logged.
Switch logs operation mode and read-only CPU/GPU/memory rates at startup and periodically during a session;
zero means unavailable. An emulator or PC run cannot prove a Switch CPU gain.

## Tests and remaining checks

- **Tested, PC**: two copies joined through the discovered list, raced to a finish capture, and kept all compared
  checksums equal through frame 25,200 with cache enabled (finish confirmed in the port capture). Both cache
  counters reached 950/1200 reads served from memory. An earlier short run stayed equal through frame 6,600.
- **Tested, PC/OLED**: keyboard/name entry, hosting, discovery/join, loading overlay, Mode Select at frame 780,
  and racing. All reported checksums matched through frame 17,400; the bounded PC test then closed. Both served
  457/600 disc reads from memory. Mode/clocks were not recorded in that first run: no hardware speed claim.
- **Tested, S23 Ultra**: update installed with SHA-256 readback; Open Online replaced the process and reached the
  pre-game lobby. PC/Android race segment: both logs agree through frame 6,600; no desync or crash.
- **Tested, final OLED build**: handheld, CPU 1785 / GPU 614.4 / MEM 2133 MHz; menu synchronization and cache
  first hit confirmed, no core-affinity failure after changing to core 2. This short check did not repeat the race.
- **Tested, Eden v0.1.1**: original Switch lobby rendered in the port's own capture. Not hardware performance.
- **Tested, synthetic data**: interleaved streams, file boundaries, short reads, EOF, large reads and mod bypass
  returned the expected bytes while the actual cache worker ran.
- **Tested, actual PC host protocol**: malformed packets, discovery, incompatible lobby/game rejection, and
  acceptance of a different build string with the same compatibility number.
- **Still needed**: native Switch 2 hardware, slow-SD stutter comparison, cold package tests (ask before Sandbox),
  user acceptance and explicit permission before a release, issue post or Discussions thread.

Test helpers: `tools/test_online_protocol.py`, `tools/net_route_2p.py`, `tests/disc_cache_test.c`.
