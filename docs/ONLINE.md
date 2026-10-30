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
| 1 | **Determinism**: two runs with the same inputs give byte-identical game state every frame, on PC and on Switch, and PC = Switch | In progress (`SR_DETCLOCK`) |
| 2 | Input recording and replay (also gives replays and ghosts); two copies in lockstep on one PC | |
| 3 | Online races PC-PC and Switch-Switch, direct connection, one player per machine, own view full screen | |
| 4 | Cross-play PC-Switch | |
| 5 | Lobby server with NAT hole punching and a relay fallback (no port forwarding), self-hostable | |
| 6 | Optional: rollback | |

## Determinism: findings

- `SR_RAMHASH=<frames>` logs a checksum of each 1 MB block of game memory at every frame end
  (`[hash] <frame> ...`); `SR_RAMDUMPAT=<frame>` (with SR_RAMHASH) writes all of game memory at that
  frame to `build/ramstep.bin`. Compare two runs (or a PC and a Switch log) line by line.
- 2026-10-29, PC, attract demo, no input, `SR_FRAMERATE=0`: two runs differ from the first frame. At
  frame 0 only 11 words differ, all clock readings: the OS start time, an alarm/timeout value, a disc
  timing value and one game variable. Cause: the port gives the game the real PC clock (`mftb`,
  `ppc_timebase()` in runtime/src/cpu.c); on a GameCube or in Dolphin the clock follows emulated time.
  The differences spread to most of memory within a few hundred frames.
- **`SR_DETCLOCK=1` (deterministic mode, 2026-10-29)**, off by default; everything below applies only with it:
  - The time the game reads (`mftb`, OSGetTime) follows game frames: each frame end adds 1/60 s; within
    a frame each guest thread counts its own reads (cpu.c). The RTC date is fixed.
  - Disc reads land at once and complete at a fixed point: the next frame end, the CPU going idle, or
    the frame handoff below (di.c).
  - Controllers are latched once per frame at the frame end; `SR_INPUT` is timed in game frames (si.c,
    input_sdl.c).
  - Retraces: one per frame, handed to the main thread once it sleeps in its frame wait (vi.c); the
    frame's audio interrupts first (a fixed quota per frame from the 32 kHz rate, dsp.c), and any disc
    read in flight completes first. A backstop delivers retraces on the host clock when the main thread
    has not waited for 10 fields (loading; code that polls the retrace count). `[det]` log lines count
    handovers, backstops and what held them.
  - Audio mixing is synchronous (its completion interrupt fires at a fixed point); OS alarms
    (decrementer) count game time and fire at frame ends, with a host-clock backstop 50 ms late.
  - Races run at 60 fps in this mode on PC with every retrace handed over (no backstop).
- **Measured (PC, scripted route to a 1P Metal City race, two runs):** the first frames are identical;
  the race scene starts within 1 frame (steps 5146 / 5147, was 22+ apart); the race data blocks first
  differ ~1000 race frames in (was 286), but memory already differs when the race scene starts, because
  the menus and loading still use the backstop now and then and audio threads' stacks/timestamps differ.
  `SR_RAMHASH` itself (a 28 MB checksum per frame) slows frames enough to trigger the backstop.
- Tools: `SR_RAMHASH_KEY=<address>` logs a u32 with each checksum line (8021BB64 counts frames in the
  current scene; it restarts per scene) and `SR_RAMDUMPKEY=<value>` dumps RAM when it reaches a value;
  `SR_RAMDUMPAT` takes a list of frames.
- Next: find what differs at the race scene's first frame (a diff of the two dumps at scene frame 0,
  REL data around 0x81Axxxxx and the 0x802040D4 block), make the menus/loading use no backstop (the code
  that polls the retrace count), compare only game state (not the audio threads' stacks), then a PC
  vs Switch run (cross-play), then the race-start barrier and lockstep.
- **Where the backstop fired (fixed, 2026-10-29 evening):** during loading the game switches to its own
  30 fps mode, and its frame end (`8000610C`) then busy-waits until the retrace count reaches the last
  present + 2, without calling VIWaitForRetrace (`L_800061C8` in the generated code). The handoff now
  also delivers while the main thread spins there (seen in the frame end on two checks in a row); when
  the main thread is not running it must be asleep inside VIWaitForRetrace. (Correction: the
  `8003062C` calls in the frame end are not a GPU wait; they run a list of EFB Z reads, likely the lens
  flare's visibility checks, issue #7.) Backstops fell from 17-55 per 600 retraces during loading to
  ~6 at boot (before the game's threads exist) and 1-2 in one later transition (main in `8000BB6C`).
- **Measured after that (PC, two runs each):** the race scene starts on the same game frame in both
  runs (5146 / 5146). One pair of runs kept the race data blocks (18-22 MB) identical for all 2,854
  race frames; another pair, with steering inputs during the race, differed from the scene's first
  frame. So the remaining nondeterminism is intermittent: the few backstops left, and the order in
  which the audio threads (which run on their own interrupts) interleave with the rest.
- `SR_SHOTSTEP=<frames>` takes screenshots at game frames (not presented frames). Pictures from two runs
  can still be a frame or two apart, because the render thread presents later than the game frame.
- Next: find the remaining sources with a dump at the race scene's first frame in two runs that differ
  there (`SR_RAMDUMPKEY` with a step condition), remove the last backstops (`8000BB6C`), then decide which
  memory must match (game state) and which may differ (audio threads' stacks and timestamps).
- **Idle ticks (2026-10-29, late):** when every game thread is blocked (the OS idles in SelectThread),
  frames have stopped (no frame wait for 3 fields) and the GPU has no queued work, 1/60 s of game time
  passes at that idle point (clock step, the audio quota, due alarms, then a retrace). This removed the
  boot-time backstops in some runs; races stay at 60 fps. 6 backstops remain at boot (OS idle while a
  disc read is pending; completing the read there instead upset the boot sequence: 23 backstops,
  reverted).
- **Status after the last round [measured]:** the race scene starts within 0-1 frames between runs, and
  in one pair of runs the race data stayed identical for the whole race, but not reliably: other pairs
  differ from the race scene's first frame (frame counters 5 apart = retraces delivered by the boot
  backstops). `SR_RAMHASH` itself makes frames slow enough to add backstops, so measurements disturb
  what they measure (a cheaper or sampled checksum, or one over the game-state blocks only, is needed).
- **Assessment:** whole-system determinism of a natively run game with host threads (audio, disc, GPU)
  is a long road; every source found so far had a fix, but the boot/loading phase keeps producing new
  ones. Two ways forward: (a) keep removing sources (the boot idle/disc case next), or (b) aim lower:
  synchronize at the race start and make only the race deterministic, which needs the race state
  (racers, RNG, timers) identified in memory so it can be checked, and if needed copied, at the barrier.
- Cross-play needs floating point to match exactly between x86 (PC) and ARM (Switch): same rounding,
  no fused multiply-add contraction by the compilers, same approximation tables (`fres`, `frsqrte`).
