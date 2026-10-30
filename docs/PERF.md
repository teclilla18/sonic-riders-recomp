# Performance test

A fixed, repeatable run to compare builds: the same scenes every time, no save and no input needed, on PC
and on the Switch. Use it before and after a change that may cost speed, and before each release (keep the
result in `perf/results/`).

## What it measures

With `SR_PERFTEST=1` the game:

- boots on a fresh memory card kept in memory (your save is not read or written),
- ignores the controllers and skips the intro movie,
- waits on the title screen until the game plays its demo races by itself, and measures 30 s of each
  (scene frames 240-2039):
  - `demo_1p`: Metal City, 1 player,
  - `demo_2p`: a 2-player split-screen race,
- writes the results to the log (`[perftest]` lines) and to `perftest.json`, then the PC build closes; the
  Switch shows "Performance test finished" (close the game from HOME).

A run takes about 3 minutes. Per scene:

| Value | Meaning |
|---|---|
| speed | game frames per second; 60 = full speed, less is slow motion |
| fps | pictures shown per second |
| game ms mean / p95 / max | the game thread's time per game step, from one frame end to the next (without the frame end's wait for the next retrace; waits inside the frame, e.g. for the GPU, count) |
| steps >16.7ms | game steps whose work alone took longer than a 60 Hz frame |

The frame rate setting (Settings > Frame rate) and the resolution are the player's, as when playing; the
result file records them. On a PC that easily runs at 60, speed and fps sit at 59.9 and the game thread's
time is the number to watch.

## On PC

```
python tools/perf_test.py run --runs 3
python tools/perf_test.py run --runs 3 --expect perf/results/<record>-pc.json
```

The game window stays minimized and silent. `--save perf/results/<name>.json` keeps the result.

## On the Switch

1. Put `SR_PERFTEST=1` in `sdmc:/switch/sonicriders/env.txt` (keep a copy of your own env.txt).
2. Start the game from the HOME menu and leave it for about 3 minutes, until "Performance test finished".
3. Close it from HOME, copy `sdmc:/switch/sonicriders/perftest.json` to the PC, and remove the line from env.txt.
4. `python tools/perf_test.py show perftest.json --expect perf/results/<record>-switch.json`

Several runs can be combined: `show run1.json run2.json run3.json --save ...`. Write down the clocks: a
record made with an overclock says nothing about stock clocks.

## Reading `--expect`

It compares each scene with a saved record and prints `SLOWER than the record` when the speed or the frame
rate is lower by more than `--tolerance` (default 1.0 per second), or the game thread's mean time is more
than 10% (and 0.2 ms) higher. It is a check across sessions, meaningful only on the same machine with the same
settings and clocks (it warns when the settings differ). Runs on one PC vary by about 0.2 ms in the game
thread's mean; a difference inside that is not a result.

## Records (`perf/results/`)

| File | Build | Machine | Notes |
|---|---|---|---|
| `2026-10-30-pc.json` | 236a1dc+ | the maintainer's PC | 3 runs; 60 fps (30 when busy); full speed in both scenes, game thread ~2.4 ms |
| `2026-10-30-switch.json` | 236a1dc+ | the maintainer's Switch, overclocked (CPU 1785 / GPU 1152 / MEM 2133) | 1 run, frame rate Auto; full speed in both; fps 58.1 (1P) and 56.4 (2P); game thread 13.5 / 13.7 ms, 77 / 385 steps over 16.7 ms |

Add one per release (`<release>-pc.json`, `<release>-switch.json`) and a line here.
