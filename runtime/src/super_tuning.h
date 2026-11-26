/* Super Sonic tuning (Riders Crosswake): every balance number in one place, two presets.
 *   balanced (default, 2026-11-25, the user's spec for the four-player Online Beta): stats a little above vanilla,
 *     boost 270 / 280 / 300 km/h, no held-boost climb, rings back only from the ring magnet and (much smaller)
 *     trick landings: nothing from grinding, flying, smashed objects or spin turns.
 *   classic (option "Classic Super Sonic"): the numbers of v0.9.0, when he first joined.
 * Online the host's choice is used by everyone (netplay.c WELCOME / recording header), so both presets are game
 * state: a change here must bump NET_COMPAT (netplay.c) if two different builds could meet online.
 * Used by game_levels.c (stats, tiers, ring rewards) and boost_hold.c (held boost). */
#ifndef SUPER_TUNING_H
#define SUPER_TUNING_H
#include <stdint.h>

typedef struct SuperTune {
    float top[3], drift[3], boost[3];   /* km/h at base / Lv. 4 / Lv. 5 (vanilla Super Sonic: 182, 100, 250) */
    uint32_t lv4_rings, lv5_rings;      /* rings for his Lv. 4 / Lv. 5 */
    int hold_climb;                     /* held boost climbs to 350 km/h (classic) or stays at the boost speed */
    uint32_t hold_cost_num, hold_cost_den;  /* held boost costs this times the gear's rate */
    uint32_t replenish_num, replenish_den;  /* smashed objects / air rewards: share kept */
    uint32_t reward_max_rings;          /* one step's smashed-object rewards count up to this many rings */
    uint32_t spin_fuel;                 /* fuel per counted spin turn (1100 = about 1 ring) */
    uint32_t grind_num, grind_den;      /* grinding: share kept */
    uint32_t fly_knee, fly_growth_num, fly_growth_den, fly_num, fly_den;  /* flying: growth cut above the knee */
    uint32_t trick_x_reward, trick_x_rings;  /* the game's X-rank landing reward maps to this many rings */
} SuperTune;

static const SuperTune k_super_balanced = {
    {186.0f, 190.0f, 195.0f}, {102.0f, 105.0f, 108.0f}, {270.0f, 280.0f, 300.0f},
    200u, 300u,
    0, 45u, 10u,
    0u, 1u, 0u, 0u,               /* no smashed-object / air rewards, no spin turns */
    0u, 1u,                       /* no grinding */
    260u, 3u, 4u, 0u, 1u,         /* no flying */
    100000u, 25u,                 /* X rank: 25 rings (classic 75) */
};

static const SuperTune k_super_classic = {   /* v0.9.0 */
    {195.0f, 220.0f, 240.0f}, {110.0f, 125.0f, 140.0f}, {270.0f, 300.0f, 330.0f},
    200u, 300u,
    1, 29u, 10u,
    1u, 2u, 8u, 1600u,
    7u, 10u,
    260u, 3u, 4u, 9u, 20u,
    100000u, 75u,
};

int settings_super_classic(void);  /* settings.c: the option, or the room's / recording's value */
static inline const SuperTune* super_tune(void) {
    return settings_super_classic() ? &k_super_classic : &k_super_balanced;
}

#endif
