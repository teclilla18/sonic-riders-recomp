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
- **Where the backstop still fires [confirmed, `[det] backstop` log lines]:** only during loading,
  always with the main thread inside the frame end (`8000610C`, called from the loading loop at
  `819BC5B8`), polling `8003062C` = an EFB pixel read (`0xC8000000` + x/y): on that path the frame end
  waits for the GPU by polling a framebuffer pixel, with a retrace as the way out. The port does not
  model EFB reads (mmio.c returns 0; the same gap as the missing lens flare, issue #7), so only a
  retrace ends the loop, and in this mode that is the host-clock backstop. Menus and races use no
  backstop (`[det]`: 600 of 600 retraces handed over). Next: real EFB peeks (flush the GP, read the
  EFB; also fixes #7), which lets this wait end at a fixed point.
- Cross-play needs floating point to match exactly between x86 (PC) and ARM (Switch): same rounding,
  no fused multiply-add contraction by the compilers, same approximation tables (`fres`, `frsqrte`).
