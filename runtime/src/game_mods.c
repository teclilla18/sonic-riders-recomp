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

/* Once a frame: a racer's live per-level stats (player record +0x8DC, three blocks of 0x38 bytes,
 * boost speed at +0x34 in each) are filled at race start, and Super Sonic's not from the gear table
 * (the transformation makes him a separate character, Sonic + 14), so the boost is set there too.
 * Only values that look like boost speeds (150-400 shown) are replaced: outside a race this memory
 * holds other things. */
void game_mods_frame(void) {
    if (!g_linked) return;  /* before OSLink the REL's bss is other memory (the OS arena) */
    /* the Chaos Emerald gear unlocked: its status byte kept "owned" as the save loads */
    if (g_settings.chaos_emerald && ld8(GAME_GEAR_STATUS + GAME_GEAR_CHAOS_EMERALD) == 0)
        st8(GAME_GEAR_STATUS + GAME_GEAR_CHAOS_EMERALD, 2);
    if (!g_settings.super_sonic_fast) return;
    for (int i = 0; i < 8; i++) {
        uint32_t p = GAME_PLAYERS + (uint32_t)i * GAME_PLAYER_SIZE;
        if (ld8(p + 0x1034) != GAME_GEAR_CHAOS_EMERALD) continue;
        for (int l = 0; l < 3; l++) {
            uint32_t ea = p + 0x8DC + 0x38u * (uint32_t)l + 0x34;
            float shown = SHOWN(getf(ea));
            if (shown >= 150.0f && shown <= 400.0f && shown != 350.0f) setf(ea, 350.0f / 216.0f);
        }
    }
}

void game_mods_linked(void) {
    uint32_t emerald = GEAR_RECORD(GAME_GEAR_CHAOS_EMERALD);
    g_orig_topmod = getf(emerald + GEAR_TOP_SPEED_MOD);
    for (int l = 0; l < 3; l++) g_orig_boost[l] = getf(emerald + GEAR_LEVEL_BOOST(l));
    g_linked = 1;
    game_mods_apply();
}
