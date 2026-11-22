# Super Sonic balance pass (2026-11-22, branch claude/super-balance)

Players said Super Sonic (Riders Plus: Chaos Emerald gear) is far too strong. Every tuning number now lives in
one header, `runtime/src/super_tuning.h` (held boost: boost_hold.c; Ring Replenish, spin fuel, ring rewards:
game_levels.c). A retune is a one-line change there.

## The cut (user's default plan)

| Number | Was | Now |
|---|---|---|
| Held-boost cap `SUPER_CAP_KMH` | 350 km/h | 300 km/h |
| Ramp to the cap `SUPER_RAMP` | 240 frames (4 s) | 300 frames (5 s) |
| Ring Replenish share `REPLENISH_NUM/DEN` | 1/2 (50 %) | 1/3 (33 %) |
| Fuel per spin turn `SPIN_FUEL` | 1600 (about 1.45 rings) | 1100 (about 1 ring) |
| Magnet, held-boost cost (2.9x), per-kind shares | | unchanged |

The F1 help text of Hold to boost says 300 km/h. Online: the game build is compared at the lobby, so two
different tunings never meet; no NET_COMPAT change needed on main.

## How it was measured (PC, minimized, deterministic)

`SR_ONLINE_SAVE=1 SR_ONLINE_RULES=1 SR_BALLOG=1 SR_BOOSTLOG=1 SR_MINIMIZED=1 SR_NOAUDIO=1
SR_INPUT=$(py -3 tools/balance_route.py super|sonic|knuckles) build/host-mingw/sonicriders.exe > log`

- Free Race, Metal City, 3 laps, 7 CPU racers, Riders Plus defaults (ring cap 999, Auto spin, Hold to boost,
  Ring Replenish on). Same input for every racer: stick forward from the countdown (the track's walls carry a
  straight run round all three laps), B held 5 s every 10 s. No steering and no tricks, so the times are not a
  player's: they compare builds, not skill. Trick payouts are not exercised by this route.
- The run is deterministic: a repeat run and the three runs in parallel gave the same finish to the frame.
  Sonic and Knuckles finished identically on both builds (the cut touches only Super Sonic) [confirmed].
- `SR_BALLOG=1` (game_levels.c bal_log): player 1 each second, each lap (laps done = racer **+0x102B**, found by
  a byte scan over a whole race; +0x102A counts the start line too) and a finish summary. Lap 1 includes about
  8 s before the start (the counter starts with the race phase, before the countdown).
- Rings = fuel / 1100 for Super Sonic (30 rings = fuel 33000).

## Results

| Racer (gear) | Lap 1 | Lap 2 | Lap 3 | Finish | Max rings | Rings gained / spent | Held boost: top speed, boosts started |
|---|---|---|---|---|---|---|---|
| Super Sonic, before | 64.22 | 44.88 | 46.77 | **155.90 s** | 132 | 550 / 480 | 310 km/h, 9 |
| Super Sonic, after | 65.38 | 54.65 | 47.27 | **167.33 s** | 120 | 449 / 379 | 288 km/h, 4 |
| Sonic (Blue Star), both | 75.92 | 66.07 | 64.80 | 206.82 s | | | 230 km/h, 12 |
| Knuckles (default), both | 82.42 | 63.98 | 60.92 | 207.35 s | | | 229 km/h, 9 |

- Super Sonic is 11.4 s (7.3 %) slower over the race; his lead over normal Sonic on this route shrinks from 50.9 s
  to 39.5 s. Rings gained fall 18 %; with fewer rings he starts fewer boosts (boost frames 1281 -> 894).
- Rings at each lap's end: before 75 / 45 / 28, after 54 / 44 / 35.
- He is still clearly the fastest racer on this route [confirmed on PC]. Whether the cut feels right in play is
  for the user to judge on a Switch [uncertain until played].

## Second and third pass (same day, user's direction)

- User: "his top boost speed isn't the problem, his base stats are". Base stats (game_levels.c k_super, now
  SUPER_TOP/DRIFT/BOOST_* in super_tuning.h) near vanilla 182/100/250: top 186/190/195, drift 102/105/108, boost
  255/260/265 (was 195/220/240, 110/125/140, 270/300/330).
- After playing: held-boost top per tier 270 / 290 / 325 km/h (was 350 for all); Super's tiers at 175 / 250 rings
  (others keep 200 / 300); flying through air dash rings (site 8187AC90) 45 % -> 15 %.

| Super Sonic build (same route) | Finish |
|---|---|
| Original Riders Plus | 155.90 s |
| First pass (cap 300, ring cuts) | 167.33 s |
| Near-vanilla stats, cap 350, ring cuts | 159.38 s |
| Same with Hold to boost off | 177.62 s |
| **Third pass (tier caps 270/290/325, tiers 175/250, flying 15 %)** | **171.97 s** |
| Normal Sonic (Blue Star) | 206.82 s |
| Vanilla Super Sonic (Riders Plus off) | lap 1 57.65 s, out of rings at ~69 s, never finished |

- On this route the held boost was worth ~18 s and the base stats ~3.5 s; vanilla Super is already the fastest racer
  until his rings run out, so the ring economy is what removes his drawback [confirmed on this route; no steering or
  tricks, so not a player's race].
- Third pass: max rings 173 (never reached the 175 tier here), held boosts ended at 259-262 km/h, flying paid 0
  rings on this route (it does not take the flight paths), so the flying cut is unmeasured here [uncertain].
- Possible double pay to check: site 818D2528 pays 5000 x 1/3 per event in spin sections (60 events, 91 rings) on
  top of SPIN_FUEL [uncertain].

Logs (local, not in git): `.local/super-balance/build/bal/{before,after}-{super,sonic,knuckles}.log`.
