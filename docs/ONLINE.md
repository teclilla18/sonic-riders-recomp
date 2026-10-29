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
| 1 | **Determinism**: two runs with the same inputs give byte-identical game state every frame, on PC and on Switch, and PC = Switch | Started |
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
- Next: a deterministic clock mode (time advances a fixed amount per game frame, not with the real
  clock), with the runtime's own scheduling (audio, disc completions, retrace timing) kept consistent;
  then find the remaining sources (disc completion timing, audio/streaming threads writing game
  memory, the frame-rate modes' skipped frames), until two PC runs match, then PC against Switch.
- Cross-play needs floating point to match exactly between x86 (PC) and ARM (Switch): same rounding,
  no fused multiply-add contraction by the compilers, same approximation tables (`fres`, `frsqrte`).
