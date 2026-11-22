/* Super Sonic tuning (Riders Plus): every balance number in one place, so a retune is a one-line change here.
 * Used by boost_hold.c (held boost) and game_levels.c (Ring Replenish, spin fuel, ring rewards).
 * All of it changes game memory on game steps, so online rooms and recordings stay in step: a change here must
 * also bump NET_COMPAT (netplay.c) if two different builds could meet online.
 *
 * Balance pass 2026-11-22 (players: "way too strong"); the previous value is in each comment. Numbers measured
 * on PC before/after: docs/notes/super-balance.md. */
#ifndef SUPER_TUNING_H
#define SUPER_TUNING_H

/* ---- held boost (boost_hold.c) ---- */
#define SUPER_CAP_KMH        300.0  /* top of his held-boost climb, km/h (was 350) */
#define SUPER_RAMP           300    /* frames held to reach the cap: 5 s (was 240 = 4 s) */
#define SUPER_HOLD_COST_NUM  29     /* held boost costs 2.9x the gear's rate (unchanged; 1.9x -> 2.3x -> 2.9x) */
#define SUPER_HOLD_COST_DEN  10

/* ---- Ring Replenish option (game_levels.c) ---- */
#define REPLENISH_NUM        1      /* rings grow at this share of the air a move gives: 33 % (was 1/2 = 50 %; */
#define REPLENISH_DEN        3      /*   30 % was once felt "too nerfed ... pointless", 2026-11-20) */
#define REWARD_MAX_RINGS     8u     /* one step's smashed-object rewards count up to this many rings (unchanged) */
#define SPIN_FUEL            1100u  /* fuel per counted spin turn: about 1 ring (was 1600 = 1.45 rings) */

/* per-kind shares with Ring Replenish (unchanged in this pass) */
#define RW_GRIND_NUM         7u     /* grinding: 70 % */
#define RW_GRIND_DEN         10u
#define FLY_KNEE             260u   /* flying: growth above this per-frame reward ... */
#define FLY_GROWTH_NUM       3u     /* ... is cut to 75 % */
#define FLY_GROWTH_DEN       4u
#define RW_FLY_NUM           9u     /* then 45 % of the whole */
#define RW_FLY_DEN           20u
#define TRICK_X_REWARD       100000u  /* the game's landing reward for an X rank ... */
#define TRICK_X_RINGS        75u      /* ... maps to this many rings */

/* The ring magnet (an invisible Magnetic Barrier, game_levels.c lv_magnet_*) has no number: it is on or off. */

#endif
