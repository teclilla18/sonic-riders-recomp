/* Gameplay options (settings.h) applied to the game's own data while it runs; the disc is untouched.
 *
 * The Extreme Gear table (_Main.rel data, 41 records of 0x1D0 bytes at GAME_GEAR_TABLE): per gear,
 * +0x00 the characters that may ride it (bit mask), +0x10 a top speed modifier, and three per-level
 * blocks of 0x1C bytes at +0x64/+0x80/+0x9C (max air, air drain, drift drain, boost cost, tornado
 * cost, a speed, boost speed at +0x18). Speeds are game units per frame; the speedometer shows them
 * multiplied by 216. Super Sonic is Sonic on the Chaos Emerald gear (18); he runs on rings and stays
 * at level 1, whose block holds his boost (250 shown). */
#include <string.h>

#include "game_addrs.h"
#include "runtime.h"
#include "settings.h"

#define GEAR_RECORD(g) (GAME_GEAR_TABLE + (uint32_t)(g) * 0x1D0u)
#define GEAR_TOP_SPEED_MOD 0x10u
#define GEAR_LEVEL_BOOST(l) (0x64u + 0x1Cu * (uint32_t)(l) + 0x18u)
#define SHOWN(units) ((units) * 216.0f)

static int g_linked;  /* _Main.rel is in place (OSLink) */
static float g_orig_topmod, g_orig_boost[3];

static float getf(uint32_t ea) {
    uint32_t u = ld32(ea);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static void setf(uint32_t ea, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    st32(ea, u);
}

/* after a settings change, or once the module is linked */
void game_mods_apply(void) {
    if (!g_linked) return;
    uint32_t emerald = GEAR_RECORD(GAME_GEAR_CHAOS_EMERALD);
    /* faster Super Sonic: top speed +50, boost 350 at every level (shown units) */
    setf(emerald + GEAR_TOP_SPEED_MOD, g_settings.super_sonic_fast ? g_orig_topmod + 50.0f / 216.0f : g_orig_topmod);
    for (int l = 0; l < 3; l++)
        setf(emerald + GEAR_LEVEL_BOOST(l), g_settings.super_sonic_fast ? 350.0f / 216.0f : g_orig_boost[l]);
    rt_log("[mods] Super Sonic: top speed modifier %+.0f, boost %.0f/%.0f/%.0f", SHOWN(getf(emerald + GEAR_TOP_SPEED_MOD)),
           SHOWN(getf(emerald + GEAR_LEVEL_BOOST(0))), SHOWN(getf(emerald + GEAR_LEVEL_BOOST(1))),
           SHOWN(getf(emerald + GEAR_LEVEL_BOOST(2))));
}

void game_mods_linked(void) {
    uint32_t emerald = GEAR_RECORD(GAME_GEAR_CHAOS_EMERALD);
    g_orig_topmod = getf(emerald + GEAR_TOP_SPEED_MOD);
    for (int l = 0; l < 3; l++) g_orig_boost[l] = getf(emerald + GEAR_LEVEL_BOOST(l));
    g_linked = 1;
    game_mods_apply();
}
