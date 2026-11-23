/* Super Sonic tuning (Riders Plus): every balance number in one place, so a retune is a one-line change here.
 * Used by boost_hold.c (held boost) and game_levels.c (Ring Replenish, spin fuel, ring rewards).
 * All of it changes game memory on game steps, so online rooms and recordings stay in step: a change here must
 * also bump NET_COMPAT (netplay.c) if two different builds could meet online.
 *
 * Balance passes 2026-11-22 (players: "way too strong"); the previous value is in each comment. Numbers measured
 * on PC before/after: docs/notes/super-balance.md. Second pass (user): the held boost's top speed is not the
 * problem, his base stats are: back almost to vanilla, a little better. */
#ifndef SUPER_TUNING_H
#define SUPER_TUNING_H

/* ---- base stats (game_levels.c k_super), km/h ----
 * Vanilla Super Sonic: top 182, drift dash 100, boost 250. Riders Plus gives him three tiers: from the start
 * (BASE), at 200 rings (LV4) and 300 rings (LV5). The game's own level blocks are scaled to these. */
#define SUPER_TOP_BASE       186.0f  /* was 195 */
#define SUPER_TOP_LV4        190.0f  /* was 220 */
#define SUPER_TOP_LV5        195.0f  /* was 240 */
#define SUPER_DRIFT_BASE     102.0f  /* was 110 */
#define SUPER_DRIFT_LV4      105.0f  /* was 125 */
#define SUPER_DRIFT_LV5      108.0f  /* was 140 */
/* boost speed = where a boost starts (user 2026-11-22: Lv. 1 / Lv. 4 reach 270 / 290 at once and stay there;
 * Lv. 5 starts at 300 and climbs while held, SUPER_CAP_LV5_KMH) */
#define SUPER_BOOST_BASE     270.0f  /* was 270 (first Riders Plus), 255 in the second pass */
#define SUPER_BOOST_LV4      290.0f  /* was 300 */
#define SUPER_BOOST_LV5      300.0f  /* was 330 */

/* rings for his Lv. 4 / Lv. 5 tiers (other racers: 200 / 300, game_levels.c LV4_RINGS / LV5_RINGS) */
#define SUPER_LV4_RINGS      175u
#define SUPER_LV5_RINGS      250u

/* ---- held boost (boost_hold.c): the top of his held-boost climb per tier, km/h. A top equal to the boost speed
 * means no climb. The tier when the boost starts decides its top (rings burnt while holding would otherwise drop
 * him a tier and the top with it). ---- */
#define SUPER_CAP_BASE_KMH   270.0
#define SUPER_CAP_LV4_KMH    290.0
#define SUPER_CAP_LV5_KMH    350.0
#define SUPER_RAMP           90     /* frames held to reach the top: 1.5 s (was 240 = 4 s: "climb much faster") */
#define SUPER_HOLD_COST_NUM  45     /* held boost costs 4.5x the gear's rate (user 2026-11-22: "punishing";
                                       was 1.9x -> 2.3x -> 2.9x) */
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
#define RW_FLY_NUM           3u     /* then 15 % of the whole (was 45 %: the user, 2026-11-22, flying through the */
#define RW_FLY_DEN           20u    /*   air dash rings was "the most broken thing" in a playthrough) */
#define TRICK_X_REWARD       100000u  /* the game's landing reward for an X rank ... */
#define TRICK_X_RINGS        75u      /* ... maps to this many rings */

/* The ring magnet (an invisible Magnetic Barrier, game_levels.c lv_magnet_*) has no number: it is on or off. */

#endif
