# Online play (design, in progress)

Goal: race other players over the internet, PC and Switch together (cross-play).

## Rules

- **One player per machine.** Online races are joined from single-player: each machine brings exactly
  one local racer (its first controller); other local controllers are ignored while online. The rest
  of the grid (up to 4 racers) is COM.
- **Cross-play.** PC and Switch players can race each other. Every machine runs the same game build
  and must compute exactly the same race.

## How it works

Every machine runs the whole race (all racers, including COM). Only button presses travel over the
network:

1. Each machine sends its player's buttons for frame N + D (D = input delay, a few frames).
2. A machine steps frame N only when it has every player's buttons for frame N; all machines feed the
   same controller inputs into the game, so they compute the same race.
3. Machines exchange a checksum of the game state now and then; a mismatch ends the race with a clear
   "desync" message instead of two different races.

This is delay-based lockstep, as in RingOut (SoulCalibur II recomp) and Dolphin's netplay; racing
games tolerate a few frames of delay well. Rollback (Slippi, Voldorium) can come later: the game's
29 MB of memory is too much to copy every frame on the Switch, so it needs "only what matters"
snapshots.

Each machine shows only its own racer's view, full screen (the game itself runs a split-screen race).

## Phases

| Phase | What | Status |
|---|---|---|
| 1 | **Determinism**: two runs with the same inputs give byte-identical game state every frame, on PC and on Switch, and PC = Switch | Done (`SR_DETCLOCK`): PC, Switch, and PC = Switch |
| 2 | Input recording and replay (also gives replays and ghosts); two copies in lockstep on one PC | Done (`SR_RECORD`, `SR_REPLAY`, `SR_NET`): PC; a PC recording replays identically on the Switch; lockstep on one PC |
| 3 | Online races PC-PC and Switch-Switch, direct connection, one player per machine, own view full screen | |
| 4 | Cross-play PC-Switch | |
| 5 | Lobby server with NAT hole punching and a relay fallback (no port forwarding), self-hostable | |
| 6 | Optional: rollback | |

## Determinism (phase 1)

**Status (2026-10-30) [measured]:** with `SR_DETCLOCK=1`, a scripted route to a 1P race (with steering
during the race) gives identical checksums of all 28 MB of game memory on every frame, from boot through
about 2,500 race frames: two PC runs, two Switch runs, and PC against Switch, all identical. Six more
PC runs, two of them with the GPU thread slowed 4x (`SR_GPSLOW=4`), were identical at the race start.
Floating point needed no work: x86 and ARM gave the same results.

**Save data must match.** The game reads its memory card at boot (settings, unlocks, records), so two
machines with different saves compute different games. The tests point both at the same card image
(`SR_CARD=<file>`); online play will need the same save on every machine, or a fixed one for online
races (to decide in phase 3).

`SR_DETCLOCK=1` is off by default; normal play is unchanged. What it does:

- **Game time follows game frames.** The clock the game reads (`mftb`, OSGetTime) advances 1/60 s per
  frame end; within a frame, each game thread counts its own reads. The RTC date is fixed. The audio
  sample counter and the video beam position follow game time too.
- **Outside events arrive at quiet points, in a fixed order.** A retrace, an audio interrupt, a finished
  disc read or the GPU's draw-done/token result is handed to the game only when no interrupt handler is
  running and either no game thread runs (the OS idles) or the running one sits in a known wait loop
  (code patches mark the frame end's retrace-count loop and the stage loader's disc-data loops). First
  the GPU finishes the work already queued (as if it were infinitely fast), then one kind at a time:
  GPU results, disc, audio, retrace. While the CPU idles, only the game thread hands things over. So
  where these land in the game's work does not depend on how fast the PC or Switch is.
- **Idle ticks.** When every game thread is blocked and nothing is under way that could wake one,
  1/60 s of game time passes there (boot, loading screens that wait on timers or audio).
- **No frame pacing.** The port's frame skipping and "30 when busy" switch follow the host's timing and
  change the game's step; in this mode the game's own frame end runs (including its own 30 fps mode
  while loading). A machine that cannot keep up runs the game slower instead.
- **Input** is latched once per frame; the connected controller ports are fixed for the run
  (`SR_PADS`, default port 1 only), not live hot-plugging.
- **Backstops** (host-clock fallbacks for anything not covered) log `[det] backstop` / `[det] alarm
  backstop`; in the measured runs none fired.

How it was found: `SR_RAMHASH=<frames>` logs a checksum of each 1 MB block of game memory per frame
(`SR_RAMHASH_BLOCKS` limits it), `SR_RAMHASH_KEY=8021BB64` adds the frame number within the current
scene, `SR_RAMDUMPAT` / `SR_RAMDUMPKEY` write all of game memory at chosen frames; two runs (or a PC and
a Switch run) are compared line by line. NOTES.md (2026-10-29/30) lists every source found and its fix:
the host clock, disc timing, retraces landing mid-frame, frame pacing, background loading progress,
GPU interrupts and registers, audio interrupt timing, controller hot-plugging.

Earlier assessment (2026-10-29): whole-run determinism looked like a long road, so the plan was to
synchronize at the race start and make only the race deterministic. Measuring the race state showed
the race only differed because loading and menus did, and the remaining sources turned out to be few;
whole-run determinism makes the race-start barrier simple (every machine reaches it in the same state).

PC against Switch: `SR_RAMHASH_REF=<the PC's hash log>` on the Switch compares every frame as it runs
and dumps memory where they first disagree; `SR_DETTRACE=1` logs every interrupt and disc command with
the frame and where the game was, so two machines' traces show the first event that lands differently.
On the Switch these go in `env.txt`, and `SR_INPUT` works there too.

## Recording, replay and lockstep (phase 2)

`runtime/src/netplay.c`. All of it turns `SR_DETCLOCK` on by itself. At each game frame end the controllers for
the next frame are decided once (si.c `si_det_latch`), and come from one of:

- **Live** (`SR_RECORD=<file>` also writes them to a file): the machine's controllers and `SR_INPUT`.
- **Replay** (`SR_REPLAY=<file>`): read back from a recording. The run is the recorded run again, frame for
  frame. The number of controller ports comes from the recording.
- **Lockstep** (`SR_NET=host:<port>` on one machine, `SR_NET=join:<address>:<port>` on the other; `host:<address>:<port>`
  listens on one address only, e.g. `127.0.0.1` for two copies on one PC). Two players: the host is player 1,
  the one who joins player 2, each with its own first controller. Each frame a machine sends its player's buttons
  for frame N + D (`SR_NET_DELAY` on the host, default 4 frames) over UDP (resent until acknowledged) and steps frame
  N once it has both players' buttons for N. The first D frames are neutral for both. `SR_NET_TIMEOUT` (default
  10 s) without anything from the other machine ends the session.

Frames are counted in frame ends, from 1. (Game time, `ppc_detclock_steps()`, also moves on in idle ticks
without a frame end, so it is not a frame counter; a recording keeps it per frame as a check.)

**Recording file:** a 64-byte header (`SRINPUT1`, version 1, number of ports, flags (1 = an online session),
the build version), then per frame: the game step, the state checksum, and 8 bytes per port (buttons, main
stick, C stick, L/R). Little-endian. About 1 KB per second for one port.

**State checksum:** each frame end, the checksum of one 1 MB block of game memory, block (frame mod 28); 28
frames cover all of it at 1/28 of the cost (~0.1 ms a frame on PC). A recording stores it, so a replay checks
itself every frame (`[replay] frame N DIFFERS ...`, and a line at the end). Lockstep machines send each other
their last 8 checksums with every packet; a mismatch ends the session on both machines with a message on screen
("Desync: the two games no longer match (frame N). The online session has ended.") and in the log (the frame,
the memory block, both checksums). A desync is found within 28 frames of the memory changing. After a session
ends (desync, the other player left, connection lost) the game goes on alone with the other player's controller
neutral.

**Joining:** the host checks the joiner's protocol version, game build and memory card checksum, and refuses
with the reason on both machines when they differ. During a session the game's saves stay in memory (exi.c
`exi_card_volatile`): the card file is not written.

Testing: `SR_NET_TEST_DESYNC=<frame>` changes one byte of that machine's game memory at that frame (in the DOL's
code, which only the recompiled copy runs, so the game itself is unaffected), to see the desync check work.

**Status (2026-10-30) [measured, PC]:**

- Record then replay, the determinism route (menus, 1P Metal City race with steering), 7,700 frames: every
  frame's full 28 MB checksum (`SR_RAMHASH`) equal between the recorded run and two replays (one with the GPU
  thread slowed 4x), and the replays' own checks all equal.
- Lockstep, two copies on one PC (host drives the menus as player 1, the joiner steers as player 2 during the
  race), delay 4: identical full-memory checksums on both for all 7,700 frames; both wrote byte-identical
  recordings; an offline replay of that recording matches the session frame for frame. Waiting for the other
  copy: ~0.01-0.6 ms a frame on average.
- Desync test (`SR_NET_TEST_DESYNC=3000` on the joiner): found at frame 3024 on the joiner, reported on both, the
  message shown on screen. Connection lost (joiner killed): the host waits, ends the session after the timeout,
  and goes on. A joiner with a different save: refused, with the reason on both.
- PC recording replayed on the Switch (overclocked, `SR_REPLAY` in env.txt): all 7,700 frames' full-memory
  checksums equal to the PC's, the replay's own check equal on all 7,920 frames, no backstops. So PC = Switch still
  holds after the fix below, and a recording made on one machine plays on the other. Lockstep on the Switch
  (sockets) has not run yet.
- Found on the way, fixed: two runs could differ in game memory block 26 (thread stacks; nothing else) for 4-8
  frames during loading. Hand-overs at the marked wait loops (frame end, REL loader) were made by the retrace
  thread when the marks were set, but a mark stays set while its thread is switched out, so a hand-over could
  land just as the thread came back and left the loop; and at a loop the GPU's results were only handed over if
  the GPU thread had already finished, so where a token landed depended on the GPU thread's speed. Now the loop
  heads hand over themselves, on the game thread (patches.txt -> vi.c `vi_det_loop_point`), after waiting for the
  GPU to finish its queued work. The rolling checksum would otherwise have reported false desyncs there.

Not done yet: more than two players (the protocol carries a player number; the host would relay), a two-player
race route (in the test, player 2 is connected but not in the race, so the race exercises player 1's input; the
menus see both controllers), each machine's own view full screen (phase 3).

## Save data for online races (proposal, 2026-10-30)

The game reads its memory card at boot (settings, unlocks, records), so every machine must start from the same
card image or the games differ from the first frames. Today the host refuses a joiner whose card differs.

Proposal: **the host's save for everyone, in memory only.** When a player joins, the host sends its card image
(512 KB) to the joiner; both machines boot the session from that image, and during the session nobody's saves
reach a file (already so). Everyone races with the host's unlocked characters, gear and tracks and the host's game
settings; nobody's own save changes. Nothing to set up, and it works the same PC to Switch.

Alternatives considered: requiring the same save on every machine (impractical outside tests); a fixed online save
(a fresh card has most characters and tracks locked, and shipping an unlocked save would mean distributing a file
made by the game, which this project does not do).

