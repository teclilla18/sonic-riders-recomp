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

## Internet play (invite codes, no server of ours)

`inet.c` owns one UDP socket (7777, or 7787 when taken) for STUN, the lobby and the game; `lan_lobby_handoff()`
gives it to netplay (`SR_NET=inet:host:<delay>` / `inet:join`), and `inet_close()` closes it exactly once (netplay's
failure and exit paths, or the lobby when leaving). LAN mode is unchanged (ports 7777-7779, SRLB v1).
- **Code**: the host's public IPv4 as 10 digits when its public port is 7777, else 15 digits (ip * 100000 + port).
  The public address is the STUN answer (stun.l.google.com:19302, stun.cloudflare.com:3478, sent on the shared
  socket); a UPnP mapping (cancellable worker, 2 s steps, 6 s cap, lease 2 h, renewed every 30 min, removed on
  leave/exit, external 7777 then 7787) is used only when the router's external address equals STUN's. A private or
  CGNAT router address = double NAT note. Two STUN servers seeing different ports = strict NAT note.
- **Keepalive**: one binding request every 10 s while in the lobby. Without it a carrier NAT moved an idle host to a
  new port within minutes and its shown code went stale [confirmed, S23 on a phone hotspot]; if the port still
  changes, the code updates and the screen says so.
- **Joining**: JOIN 10/s for 3 s then 2/s, for 90 s; the host answers WAIT and shows "<name> wants to join"
  (Let them in / Say no); a refused address is refused for 60 s; one pending player; at most 8 JOINs/s handled.
  Accepted: a random 64-bit session id in LIST, PING/PONG (timestamped, every 250 ms), START and LEAVE.
- **Both codes**: a host behind a NAT that drops unsolicited packets adds the joiner's own code (shown on the
  joiner's screen): the host sends PUNCH to it (10/s for 3 s, then 4/s, up to 60 s) while the joiner keeps
  sending JOIN. Carrier NATs that change ports per destination cannot be traversed without a relay.
- **Delay**: median RTT and jitter (mean absolute deviation) of up to 16 pings; delay = ceil((rtt/2 + 2 x
  jitter) / 16.7 ms) + 1, 2..15 frames, - / + / Auto on the host; fixed when Start is pressed (carried in START and
  WELCOME). The game handshake only accepts the exact address the lobby let in.
- **Privacy**: public addresses are written as a.b.x.x in logs (logs are attached to public issues).
- Test hooks: `SR_LOBBY_TEST=inet-host` (lets the first asker in, starts after 12 pings) and
  `inet-join:<code>`; `SR_INET_TEST_IP=127.0.0.1` makes codes loopback for two copies on one PC.

- **Flight**: Super Sonic flight follows the host (WELCOME byte 6 bit 3; recording header flag 64; NET_COMPAT 2).
- **Texture packs online**: a pack's first-use lookups run on the game thread, so a slow device stalls both
  players (slow-motion countdown). Advice until loading moves to a worker: packs off for online races.
- GXSRTE blanks the game's racer-marker sheet (`tex1_256x256_d8d7b29f19324bf2_843adf87433cfe86_9`, fully
  transparent): no 1P-4P / COM tags. Renaming that one file to `.png.off` brings them back (only .png/.dds load).

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
- **Tested, PC (2026-11-21)**: Windows restart to Online keeps the original command line, so an explicit disc
  path outside the game folder (and spaced paths) survives; harness from a spaced folder + game with an outside ISO
  and `--online` reached the lobby (capture checked). Back to offline (`SR_LOBBY_TEST=offline`, the button's own
  calls): the game continued offline to the intro, no `[net]` lines, card copy and original byte-identical.
- **Native package audit**: baseline is released Switch 2 Alpha 3 (67710d8, build bc9dd55883f6; user's choice
  2026-11-21); only the listed Online Beta runtime files may differ (`tools/audit_switch2_release.py`).
- **Tested, internet (2026-11-21)**: protocol test `--inet` PASS (malformed/STUN-like packets, version refusal,
  approval, session id, full room, ping/pong, START delay, handshake on the handed-over socket, stranger on the same
  IP ignored). Two PC copies (loopback codes, 7777 + 7787): 13,200 frames equal. **S23 on an iPhone hotspot
  (carrier NAT, 15-digit code) hosting, PC on home ethernet joining**: first attempts failed (stale code, see
  Keepalive; a raw UDP probe received nothing); with a fresh code and both codes: connected, ping 66 ms, delay 5,
  equal through frame 14,400. **S23 (hotspot) hosting, OLED (handheld, CPU 1785 / GPU 614.4 / MEM 2133) on home
  Wi-Fi joining** with both codes: ping 97 ms, jitter 42 ms, delay 8, equal through frame 26,328; ended cleanly
  when the S23 left. UPnP: no device on the home network answered (unproven).
- **Fixed, end of race**: the race phase turns "finished" at the FIRST player's finish; the online rules then
  silenced every player but player 1, so a player still racing lost control (OLED finished first, the S23's
  player 2 could not steer) [confirmed by the user]. Now a player is silenced only after its own racer has
  finished (racer +0xBC = 1, the game's CPU takeover at the finish). PC two-copy run: each finish logged on the
  same frame on both machines ([net] "player N crossed the finish line"), checksums equal through 24,600.
- **Still needed**: native Switch 2 hardware, slow-SD stutter comparison, cold package tests (ask before Sandbox),
  user acceptance and explicit permission before a release, issue post or Discussions thread.

Test helpers: `tools/test_online_protocol.py`, `tools/net_route_2p.py`, `tests/disc_cache_test.c`.
