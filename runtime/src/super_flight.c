/* Super Sonic in constant flight (Riders Crosswake, the user 2026-11-20: "he FEELS like Super Sonic"): off his board
 * except when grinding, in a trick jump, in a spin section or charging a jump.
 *
 * An object's (for a racer: the racer itself) playing animation is +0x764. It is taken from the object's queue
 * (+0x78C, 16-byte entries {animation, blend, next, ...}, count +0x7CC; fn 818B6748 clears the queue to one entry)
 * at 818B66CC, 818B6790 and 818B6D24, only when the animation changes. Seen in a racer dump (2026-11-20): 1 cruising,
 * 10 jump, 11 in the air, 13 about to land, 14/15 landing, 20 charging a jump, 27/28 grinding, 65 boost pose,
 * 70 landing on the board, 73 tricks. Racer state +0x1034 (byte): 5 on the board, 6 jump, 7 falling, 8..13 trick
 * jumps, 17 spin section, 18 grinding. The riding animations of a Super Sonic in those other states become
 * the chosen flying animation. SR_FLYANIM=<n> (testing): use animation n instead, and on racer 0 whatever the character.
 * SR_FLYLOG=1: log every animation request of racer 0. */
#include <stdlib.h>

#include "game_addrs.h"
#include "runtime.h"
#include "settings.h"

/* the flying animation: 34 (the user picked it on Switch with a test slider, 2026-11-20). SR_FLYANIM=<n> tries another. */
#define FLIGHT_POSE 34u

static int fly_test = -2;  /* SR_FLYANIM, -1 = not set */

static int test_anim(void) {
    if (fly_test == -2) {
        const char* e = getenv("SR_FLYANIM");
        fly_test = e && *e ? atoi(e) : -1;
    }
    return fly_test;
}

static int riding_anim(uint32_t a) {
    switch (a) {
    case 1: case 2: case 3: case 5:     /* cruising, turning */
    case 10: case 11: case 13:          /* jump, in the air, about to land */
    case 14: case 15: case 70:          /* landings */
        return 1;
    default:
        return 0;
    }
}

/* animations kept as they are while riding (the user, 2026-11-20: he only went back to flying after a jump, because
 * a boost (65), a smash or a landing pose left him on the board until the next jump animation): jump charge, crashes,
 * smashing objects, trick rank landings, finish poses, getting on / off the board */
static int keep_anim(uint32_t a) {
    /* not the trick rank landings (55/56) or smashes (32/33): the game asks for nothing after them, so he stayed on
     * the board until the next jump (Switch 1, 2026-11-20) */
    return a == 20 || a == 25 || (a >= 62 && a <= 64) || (a >= 66 && a <= 73);
}

static int riding_state(uint32_t st) { return st >= 5 && st <= 7; }  /* on the board, jumping, falling */

static int board_state(uint32_t st) {  /* on the board on purpose: tricks, spin sections, rails */
    return (st >= 8 && st <= 13) || st == 17 || st == 18;
}

uint32_t super_flight_anim(uint32_t obj, uint32_t anim) {
    uint32_t d = obj - GAME_PLAYERS;
    if (d >= 8 * GAME_PLAYER_SIZE || d % GAME_PLAYER_SIZE) return anim;  /* not a racer */
    int t = test_anim();
    uint32_t st = ld8(obj + 0x1034u);
    static int log = -1;
    if (log < 0) log = getenv("SR_FLYLOG") != NULL;
    if (d == 0 && log && anim != ld32(obj + 0x764u))
        rt_log("[flight] racer 0: animation %u asked for, state %u, was %u", anim, st, ld32(obj + 0x764u));
    int super = ld8(obj + GAME_RACER_GEAR) == GAME_GEAR_CHAOS_EMERALD &&
                ld8(obj + GAME_RACER_CHARACTER) == GAME_CHAR_SUPER_SONIC;
    if (t >= 0 ? d != 0 : !(super && settings_super_flight())) return anim;
    if (board_state(st)) return anim;
    if (riding_state(st) ? keep_anim(anim) : !riding_anim(anim)) return anim;
    return t >= 0 ? (uint32_t)t : FLIGHT_POSE;
}
