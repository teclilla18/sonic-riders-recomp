/* Auto spin (#23, Gameplay tab, off by default): in the scenes where the game asks you to turn the stick round
 * and round (air refill + speed-up), the port turns the player's stick for them, at the fastest rate the game
 * counts.
 *
 * How the game counts (input fn 818A6E60, per racer; racer +0x0 -> its controller struct, 0x30 per port from
 * 801AF9F0): the stick becomes one of four directions (racer +0xA08: 0x1000 up, 0x2000 down, 0x4000 left,
 * 0x8000 right). A step to the next direction clockwise adds 1 to a counter (+0xA20), anticlockwise subtracts 1;
 * at +4 or -4 a whole turn is counted (+0xA04 bit 31) and the counter restarts. The counter moves at most one
 * step per game frame: one quarter turn per frame (15 turns a second) is the most the game allows, and every
 * turn adds speed with no cap, which leaves the scene at about twice a fast human's speed. The user chose a
 * fast human's rate instead (2026-11-20): 5 turns a second felt like a fast hand spin (one measured 4.8), and
 * the user asked for a bit more: 7 turns a second (6 was tested; 7 picked by the user without a further test).
 * The spin scene itself sets racer +0x624 (halfword) to 1 while it runs (+0x626 counts its progress).
 *
 * Only the controller input is changed (never game memory), so the game's own refill and speed-up run as usual,
 * recordings hold the turned stick, and in online play the other side receives it as our input. */
#include <stdint.h>
#include <stdio.h>
#include "game_addrs.h"
#include "runtime.h"
#include "settings.h"

#define PAD_STRUCTS   0x801AF9F0u  /* the game's controller structs, one per port */
#define PAD_STRUCT_SZ 0x30u
#define RACER_SPIN    0x624u       /* u16: 1 while the racer is in a spin scene */
#define QUARTERS 7                /* quarter turns per 15 frames: 7 turns a second at 60 frames */

/* clockwise: up, right, down, left */
static const int8_t k_dx[4] = {0, 100, 0, -100}, k_dy[4] = {100, 0, -100, 0};

static int in_race(void) {
    static int s_was;
    uint32_t mode = ld32(GAME_TOP_MODE);
    int now = (mode == GAME_TOP_MODE_RACE || mode == GAME_TOP_MODE_MULTIPLAYER) &&
              ld32(GAME_RACE_PHASE) != GAME_RACE_FINISHED;
    if (now && !s_was) {  /* which controller drives which racer (0 = not a controller: a CPU) */
        char line[64]; int n = 0;
        for (int i = 0; i < 8; i++) {
            uint32_t pad = ld32(GAME_PLAYERS + (uint32_t)i * GAME_PLAYER_SIZE);
            int port = pad >= PAD_STRUCTS && pad < PAD_STRUCTS + 4 * PAD_STRUCT_SZ ? (int)((pad - PAD_STRUCTS) / PAD_STRUCT_SZ) + 1 : 0;
            n += snprintf(line + n, sizeof line - (size_t)n, " %d", port);
        }
        rt_log("[autospin] race: controller per racer%s", line);
    }
    s_was = now;
    return now;
}

/* the racer that controller port `port` drives, if it is in a spin scene; 0 otherwise. Logs each section once. */
static uint32_t spinning_racer(int port) {
    static uint32_t s_logged[4];  /* the racer + 1 being spun for, per port */
    uint32_t want = PAD_STRUCTS + (uint32_t)port * PAD_STRUCT_SZ, found = 0;
    int idx = -1;
    for (int i = 0; i < 8 && !found; i++) {
        uint32_t r = GAME_PLAYERS + (uint32_t)i * GAME_PLAYER_SIZE;
        if (ld32(r) == want) {
            if (ld16(r + RACER_SPIN) == 1) { found = r; idx = i; }
            break;
        }
    }
    if (found && s_logged[port] != (uint32_t)idx + 1)
        rt_log("[autospin] port %d: racer %d, spin section %d", port + 1, idx, (int)(int16_t)ld16(found + 0x628u));
    s_logged[port] = found ? (uint32_t)idx + 1 : 0;
    return found;
}

/* `frame` >= 0: the game frame this input is for (latched input); frame < 0: input read live by the game's own
 * polls, timed by the 60 Hz video field count */
/* Safety: the game counts each accepted quarter step in racer +0xA20. If that has not moved for STUCK_TICKS
 * while the stick is being turned, the game is not running a spin there (seen once, 2026-11-20: the stick stayed
 * taken until the race ended), so the player gets the stick back until the racer leaves that section. */
#define STUCK_TICKS 120  /* frames (or 60 Hz fields): 2 s */
void auto_spin_pad(int port, GcPad* p, int64_t frame) {
    static uint64_t s_moved[4];     /* tick when +0xA20 last changed */
    static uint16_t s_count[4];
    static uint8_t s_active[4], s_given_back[4];
    if (!settings_auto_spin() || port < 0 || port > 3 || !in_race()) { if (port >= 0 && port < 4) s_active[port] = 0; return; }
    uint32_t r = spinning_racer(port);
    uint64_t t = frame >= 0 ? (uint64_t)frame : vi_field_count();
    if (!r) { s_active[port] = 0; s_given_back[port] = 0; return; }
    uint16_t count = ld16(r + 0xA20u);
    if (!s_active[port] || count != s_count[port]) { s_moved[port] = t; s_count[port] = count; }
    s_active[port] = 1;
    if (s_given_back[port]) return;
    if (t > s_moved[port] + STUCK_TICKS) {
        s_given_back[port] = 1;
        rt_log("[autospin] port %d: the game counts no turns here, stick given back until the section ends", port + 1);
        return;
    }
    int next = (int)((t * QUARTERS / 15) & 3);  /* clockwise: at most a quarter per frame */
    p->stick_x = (uint8_t)(0x80 + k_dx[next]);
    p->stick_y = (uint8_t)(0x80 + k_dy[next]);
}
