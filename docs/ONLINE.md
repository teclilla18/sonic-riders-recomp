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
| 1 | **Determinism**: two runs with the same inputs give byte-identical game state every frame, on PC and on Switch, and PC = Switch | PC done (`SR_DETCLOCK`); Switch and PC = Switch not yet measured |
| 2 | Input recording and replay (also gives replays and ghosts); two copies in lockstep on one PC | |
| 3 | Online races PC-PC and Switch-Switch, direct connection, one player per machine, own view full screen | |
| 4 | Cross-play PC-Switch | |
| 5 | Lobby server with NAT hole punching and a relay fallback (no port forwarding), self-hostable | |
| 6 | Optional: rollback | |

## Determinism (phase 1)

**Status (2026-10-30) [measured, PC]:** with `SR_DETCLOCK=1`, two runs of a scripted route to a 1P race
(with steering during the race) have identical checksums of all 28 MB of game memory on every frame,
from boot through 2,400 race frames. Six runs, two of them with the GPU thread slowed 4x
(`SR_GPSLOW=4`), were identical at the race start. Not yet measured: the Switch, and PC against Switch.

`SR_DETCLOCK=1` is off by default; normal play is unchanged. What it does:

- **Game time follows game frames.** The clock the game reads (`mftb`, OSGetTime) advances 1/60 s per
  frame end; within a frame, each game thread counts its own reads. The RTC date is fixed. The audio
  sample counter and the video beam position follow game time too.
- **Outside events arrive at quiet points.** A retrace, an audio interrupt, a finished disc read or the
  GPU's draw-done/token result is handed to the game only when no interrupt handler is running and
  either no game thread runs (the OS idles) or the running one sits in a known wait loop (code patches
  mark the frame end's retrace-count loop and the stage loader's disc-data loops). So where these land
  in the game's work does not depend on how fast the PC or Switch is.
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

**Next:**
1. The same check on the Switch, then PC against Switch (cross-play). Floating point must match exactly
   between x86 and ARM: same rounding, no fused multiply-add contraction by the compilers
   (`-ffp-contract=off` is set on PC), the same approximation tables (`fres`, `frsqrte`).
2. Phase 2: record a session's inputs per frame and replay them (also gives replays and ghosts); two
   copies on one PC exchanging inputs in lockstep.
