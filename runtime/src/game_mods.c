/* Gameplay options (settings.h) applied to the game's own data while it runs; the disc is untouched.
 *
 * Data found for these (NOTES.md): the Extreme Gear table (_Main.rel data, 41 records of 0x1D0 bytes
 * at GAME_GEAR_TABLE: +0x00 the characters that may ride the gear, +0x10 a top speed modifier, three
 * per-level blocks at +0x64/+0x80/+0x9C with the boost speed at +0x18; speeds x216 = speedometer) and
 * the racers (GAME_PLAYERS). */
#include "game_addrs.h"
#include "runtime.h"
#include "settings.h"

static int g_linked;  /* _Main.rel is in place (OSLink) */
void mods_apply_link(void);   /* mods_data.c */
void mods_apply_frame(void);

void game_mods_apply(void) {}

/* once a presented frame */
void game_mods_frame(void) {
    if (!g_linked) return;  /* before OSLink the REL's bss is other memory (the OS arena) */
    mods_apply_frame();  /* Gecko codes from mods (mods_data.c) */
    /* the Chaos Emerald gear unlocked: its status byte kept "owned" as the save loads */
    if (g_settings.chaos_emerald && ld8(GAME_GEAR_STATUS + GAME_GEAR_CHAOS_EMERALD) == 0)
        st8(GAME_GEAR_STATUS + GAME_GEAR_CHAOS_EMERALD, 2);
}

void game_mods_linked(void) {
    g_linked = 1;
    mods_apply_link();  /* data mods (mods_data.c): gear stats, [memory] */
}
