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

## No CPU racers online (2026-11-22, NET_COMPAT 3)

User's rule: online races never have CPU racers. Player Entry sets the racer count 8021B8C0 (8 with 1-2 players,
else 4) at 8193F6DC; with the online rules (`netplay_no_cpus`, the same on every machine and in recordings made with
them, also `SR_ONLINE_RULES=1`) it stores r17, the human players, so only their racers are created. The runtime's
race-mode checks read the word through `GAME_MODE()` (2 counts as 8, 3 as 4). Only Player Entry's store is changed:
modes that set their own count elsewhere (World Grand Prix at 8194B4F8 [likely], Tag, Survival, story) are not
covered yet [uncertain]. Tested [confirmed, PC]: two copies over localhost (`tools/net_route_2p.py` online-p1 /
online-p2local): Metal City with only the two players (1st/2nd, two map markers), both crossed the finish (frames
19,118 / 20,322), 28,200 frames with every checksum equal on both; offline (empty card, p1 route) still 8 racers.

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

## First test between two homes (2026-11-24, user-played, PC vs PC)

Build: beta.3 zip (ab358f6) on both PCs (user + brother, about 2 hours apart). No texture packs. Logs not collected.

- **Connected with no issues** (internet invite codes) [confirmed].
- **Rings and HUD very glitchy** in the race [confirmed, cause unknown].
- **Racers vanish with distance**: after a short distance neither player could see the other's character, only
  the 1P/2P markers [confirmed, cause unknown; check whether far racers are culled/hidden by the online rules
  or the 2-racer count from NET_COMPAT 3].
- **Audio stutter** without texture packs [confirmed; maybe lockstep waits on internet ping].
- **Auto spin stuck on after a grind**: attempting a grind (an auto path) turned Auto spin on and it would not
  stop until the next auto path was completed [confirmed; likely also offline, check the auto-path end check].
- **Logs (both sides, read by log-reader):** same build, NET_COMPAT 3; user hosted. Ping ~55 ms, jitter 13 ms,
  delay 5. Two races, 54,639 frames, **all checksums equal**, network wait under 0.2 ms/frame, no audio underruns,
  60 fps. Hitches: 105 / 134, worst ~300 ms, on the same frames on both PCs and all 'game code' time [confirmed].
- **Screen shapes differed** [confirmed]: host 2.3875 (ultrawide, Fit to screen, window resized often), joiner
  1.7792; the room camera follows the host (2.3875) on both. Likely cause of the glitchy rings/HUD and the
  vanishing racer on the joiner's side [likely]. Test: both at the same shape (4:3 or 16:9); fix idea: each player
  draws with its own shape or the room uses the narrower one.
- Audio stutter probably follows the game-code hitches (audio runs on the game thread) [likely].
- Auto spin: `[autospin]` lines have no frame numbers, so the grind case can't be seen; add frame numbers and
  section enter/leave lines, then reproduce offline first.

## Beta.4 PC test

- Implemented: HELLO/WELCOME negotiate the narrower screen shape for both the camera and the rendered
  picture (NET_COMPAT 4). The PC renderer reports the window size before the handshake. Online resizes
  fit the shared picture with bars as needed.
- Implemented: Auto spin checks the movement state as well as the section flag. States 0 (observed in
  Metal City's spin section) and 17 can use Auto spin. Leaving a section resets its watchdog and returns
  manual control. Logs include the input frame, port, racer, section, state, and spin flag.
- Offline reproduction [confirmed]: a temporary `SR_POKEAT` test held the spin flag at 1 for 225 frames
  during a jump and trick (frames 6200–6424). The old code changed 215 of 225 recorded stick samples
  because the quarter-turn counter still moved. Beta.4 changed zero, including another 136-frame stale
  flag test after the natural spin. During the real Metal City spin (frames 10987–11162), stick input
  matched the original Auto spin exactly and the log showed when the section began and ended.
  An actual grind matching the first two-home report is still needed.
- Local screenshots [confirmed]: 1910×800 host and 1424×800 joiner both draw the 1.7792 picture.
  Frame 4300 shows both models and the HUD on both peers. The joiner shows track rings at 5200.
  Both racers completed normal spin sections. All checksums agreed through checkpoint 15,600
  (host compared 15,600, joiner 15,598).
  The old wider-camera diagnostic also drew readable HUD and rings on this route. Distant racer
  visibility, the reported visual glitches, and audio stutter still need the two-home retest.
- LAN and internet lobby protocol tests passed on PC, including version rejection and the game
  handshake on the internet lobby's socket.

## Four-player PC candidate

- Protocol 6 / NET_COMPAT 5. The in-game LAN and invite-code lobbies admit up to four players. The host
  starts with at least two. Guests send one pad stream each. The host bundles all inputs and compares
  checksums against each guest. The lobby passes each connected socket and its peer addresses to the race.
- The 32:9 game camera gives all copies the same visibility. Each device narrows its rendering to its
  own aspect. Lower presentation rates skip display copies while executing the original GX work.
  The PC in-between renderer still uses its local picture-rate setting.
- The original candidate enlarged a composited quadrant, including the split-screen HUD. Three-player
  rooms retain this behavior. The private four-player replacement is described below.
- PC Metal City [confirmed]: 5,400 equal input/checksum records per peer for 4P LAN, 4P invite-code
  loopback and 3P direct. Mixed local choices included 30, 45, 60 and 120 FPS. The higher rate dipped
  with four copies drawing on one computer. A 2P direct run passed 4,800. A fifth participant was
  refused by the invite-code lobby. Disconnecting a guest ended the session for the three remaining
  PCs. Cross-network and hardware groups are pending.
- Run `py -3 tools/test_online_room.py --exe <exe> --iso <own-disc.iso> --players 4 --mode lan`
  (Fit 1910×800 by default). Use `--mode inet` for invite-code loopback or `--mixed` for different windows
  and FPS. Test captures and recordings stay under the ignored `build/online-room-test/`.

## To do (user requests)

- **Number pad / keyboard digits for invite codes** (user, 2026-11-24): the code popup (`code_pad` in
  runtime/src/menu.cpp) only takes its on-screen 0-9 buttons and Paste. On PC it should also take the keyboard's
  number row and numpad (0-9, Backspace = Delete, Enter = OK); check the keys reach ImGui while the popup is open
  (input_sdl.c turns some keys into menu buttons).
- **Letter keys in text boxes** (ab358f6, 2026-11-23): W/A/S/D/J/K/Q/E type text while a menu text box is
  active (overlay_text_typing). Built on PC, not yet tried by typing.

Test helpers: `tools/test_online_protocol.py`, `tools/net_route_2p.py`, `tests/disc_cache_test.c`.

### Four-player solo HUD, private PC candidate (2026-11-25)

- The race initializer loads human count 8021B8C8 into r21 at 8183BBFC. The compare at 8183C644
  selects the solo task group. Keep r21 and the later per-player loops intact. The original scene
  registration must still use the multiplayer renderer 8180ED78. Choosing the entire solo branch
  unmodified left three cameras showing an empty track.
- The scheduler 800E2724 assigns the callback, initial state and a persistent work block.
  Current task is at 8021C314, work at task +0x10, state byte at +0x14. For the personal HUD
  the primary handle is at 81A16BC4. Four distinct tasks progressed from state 2 to 4 to 5 in the
  bounded PC trace. Use the common constructor path at 8183C6E4. Normal Race skips 8183C710.
- Each peer runs the same four instances of 8183172C (personal panel), 81826000 (lap and rank)
  and 818293C0 (gauge). Resolve each racer by its controller pointer. Native task work carries
  its animation between frames. A host-only FIFO marker follows the draws through asynchronous
  GX decoding. Foreign personal layers have an empty scissor in the completed frame.
- The map task 81818A58 selects solo placement at 81818B50. The marker loop still sees the
  full field. The camera effect 818A8E84 samples the previous EFB image. Each racer uses the
  original solo quad and its own opacity. Its former quadrant geometry left a translucent rectangle.
- The lap panel seeds both its rank animation and three empty record-time fields from the initial
  rank. In solo this is zero. The online constructor clears only work bytes +3 through +5 to
  prevent P2–P4 from displaying their starting position as minutes, seconds and hundredths.
- `SR_HUDTRACE=1` samples the four task owners at frames 5299 and 11999. Cross-check captures
  against the controller pointer, speed (+0xAAC × 216), rings (+0xB98), level (+0x102E),
  position (+0x102C, zero-based), lap (+0x102A) and air (+0x984). The timestamp on a displayed
  capture can differ by a few hundredths across local presentation rates.
- `tools/test_online_room.py --players 4 --mixed --finish --frames 23000` compares every recorded
  pad/checksum bundle and requires all peers to report the same four finish frames. The tool also
  takes simulation-step captures. Supply the executable, local disc and a fresh output directory.

- On the invite-code iteration, all 21,000 four-player input/checksum records agreed. Each peer
  recorded P2 at 18834, P4 at 18891, P1 at 18946 and P3 at 19306. These are simulation steps.
- The native multiplayer finish controller 818201C8 supplies the finishing racer to results
  parent 81931174 (work +0x20) and lap table 8192DE54 (work +0x10). Retain that controller and
  use the native solo placement for its owned panels. The original solo finish controller
  supplied the first finisher's times to every display.
- Final private Windows executable SHA-256 9aaedb7dcb7e72b5fae139111d3a3b40ceb61f5aeab90d4f33aa3bd598fc7920.
  Four direct peers matched every input/checksum bundle through frame 20500. Finishes by seat
  were 18942, 18833, 19155 and 19046. The final standings showed four distinct times in finish order.
  Final FPS samples were 60, 29.9–30.0, 114.8–120.0 and 44.6–44.9 on one Windows PC.
  Offline 16:9 solo and four-way split frame 6500 matched their prior captures byte for byte.
  Cross-device testing and personal review remain open.
