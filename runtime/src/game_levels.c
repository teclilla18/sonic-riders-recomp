/* Gear levels 4 and 5 (part of the "Riders Plus" option, formerly "Ring Limit 999": settings.c g_ring_cap != 100).
 *
 * The game keeps a racer's level in a byte at racer +0x102E (0..2 = Lv. 1..3), raised when the rings reach
 * 30 / 60 (byte table at REL rodata 0x19FC) and lowered below them, and three per-level stat blocks of
 * 0x38 bytes at racer +0x8DC / +0x914 / +0x94C (built by fn 818F2008 from the character and the gear:
 * +0x08 top speed, +0x30 drift-dash speed, +0x34 boost speed; x216 = the speedometer). About 100 functions
 * index those blocks by the level byte and the racer's air sits right after the third block, so the byte
 * never goes past 2 here. Instead, while it is 2, Lv. 4 / Lv. 5 are kept in this file and the third
 * block's speeds are raised; they are put back when the racer drops below 200 rings again.
 * Chaos Emerald is racer +0xBB (not +0x1034, an ability mask). Transformed Sonic (+0xBA == 11)
 * stays at native level 0 and uses block 1, including his own 182/100/250 baseline. Scale that block
 * without changing his native level or ring resource. The player's effect-only branch supplies feedback.
 *
 * The HUD draws the level digit as a cell of its sprite table (lbl_1_data_9F4, x / y / w / h in the 512x512
 * HUD texture; the game reads a heap copy of it): the 1P HUD (fn_1_31670) entries 0x12 + level (16x16, the
 * small "0123" row), the split-screen HUD entries 0x87 + level (20x20). The texture has no "4" or "5" in
 * those styles, so when it is decoded (gx_texture.c) the digits are drawn into empty spots of it from the
 * HUD's own orange digits, restyled like each row's "3" (white fill, dark rounded outline), and while a
 * racer is at Lv. 4 / 5 the HUD's cell pointer is pointed at table entries kept in unused low guest memory.
 * Everything is made from the player's own disc at run time.
 *
 * Reaching Lv. 4 / 5 plays the game's own level-up (sparkle, sound, air refilled): on that frame the
 * player's level-up check (fn 81836054) sees the byte at 1 and the rings over 60, so it raises it to 2 as
 * on a real Lv. 2 -> 3. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_addrs.h"
#include "gx_render.h"
#include "runtime.h"
#include "settings.h"

extern volatile int g_ring_cap;

#define LV4_RINGS 200
#define LV5_RINGS 300
/* each gear's own Lv. 3 values times these (Blue Star: top speed 180 -> 200 -> 215, boost 250 -> 275 -> 300). User
   2026-11-13: Lv. 5 at 350 rings (500 was near impossible on the Switch), both levels a little weaker in return;
   2026-11-14: still too fast, a little weaker again; 2026-11-16: Lv. 5 at 300 rings */
static const float k_top[3]   = {1.0f, 200.0f / 180.0f, 215.0f / 180.0f};
static const float k_boost[3] = {1.0f, 1.10f, 1.20f};
static const float k_drift[3] = {1.0f, 1.10f, 1.25f};
/* Super only, Riders Plus on: target top/drift/boost from the unmodified 182/100/250 live baseline.
 * Ratios preserve stat mods; always multiply the captured unscaled field, including the Base tier. */
static const float k_super[3][3] = {
    {195.0f / 182.0f, 220.0f / 182.0f, 240.0f / 182.0f},
    {110.0f / 100.0f, 125.0f / 100.0f, 140.0f / 100.0f},
    {270.0f / 250.0f, 300.0f / 250.0f, 330.0f / 250.0f},
};

#define BLK3 0x94Cu  /* the third (Lv. 3) stat block */
#define OFF_TOP 0x08u
#define OFF_DRIFT 0x30u
#define OFF_BOOST 0x34u

typedef struct {
    int ext;                     /* 0 = the game's level, 1 = Lv. 4, 2 = Lv. 5 */
    int levelup;                 /* a level-up to play (lv_player_update) */
    uint32_t base[3];            /* the game's Lv. 3 values (top, drift, boost) as float bits */
    uint32_t wrote[3];           /* what was written, to notice the game rebuilding the block */
    uint32_t block, identity;
    int valid;
    int applied;                 /* owned writes, including Super's balanced Base tier (ext == 0) */
} LvRacer;
static LvRacer g_lv[8];
static const uint32_t k_off[3] = {OFF_TOP, OFF_DRIFT, OFF_BOOST};

static int racer_index(uint32_t racer) {
    uint32_t d = racer - GAME_PLAYERS;
    if (d >= 8 * GAME_PLAYER_SIZE || d % GAME_PLAYER_SIZE) return -1;
    return (int)(d / GAME_PLAYER_SIZE);
}

static int super_racer(uint32_t racer) {
    return ld8(racer + GAME_RACER_GEAR) == GAME_GEAR_CHAOS_EMERALD &&
           ld8(racer + GAME_RACER_CHARACTER) == GAME_CHAR_SUPER_SONIC;
}

static int active_race(void) {
    uint32_t mode = ld32(GAME_TOP_MODE);
    return (mode == GAME_TOP_MODE_RACE || mode == GAME_TOP_MODE_MULTIPLAYER) &&
           ld32(GAME_RACE_PHASE) != GAME_RACE_FINISHED;
}

/* Ring Replenish (Riders Plus option): Super Sonic's rings grow at this share of the air the same move gives
 * (the user, 2026-11-20: the first build's rate was 30 % too fast). */
#define REPLENISH_NUM 1
#define REPLENISH_DEN 2   /* 50 % (fourth tuning, 2026-11-20: 30 % felt "too nerfed ... pointless") */
/* the second tuning (the user, 2026-11-20: "still too broken", chains of hoops / grinds / objects grew the
 * game's reward each time): one step's air rewards count up to this many rings before the share above, and a
 * counted spin turn gives SPIN_FUEL (the game's 5000 x 0.1, about 0.45 rings; the third tuning) */
#define REWARD_MAX_RINGS 8u   /* fourth tuning: was 4 */
#define SPIN_FUEL 1600u        /* fifth tuning (user: more in spin sections): was 1000, now about 1.45 rings a turn */
static uint32_t g_gain_racer, g_gain_start;  /* the racer the fuel step is on, and its fuel before the step */

/* The fuel step fn 81815xxx (racer r29): just before it adds the step's air rewards (+0x988) to the fuel. */
void lv_super_gain_start(uint32_t racer, uint32_t air) {
    g_gain_racer = racer;
    g_gain_start = air;
}

/* "No ring decrease" (cheat, the user 2026-11-20): a player's Super Sonic (Chaos Emerald gear) never loses rings. His
 * fuel (+0x984, rings = fuel x 100 / max air) is put back to the highest it has been this race at each fuel step, so
 * boosts, hits, falls and the steady drain cost nothing while gains still count. A new race (no step for half a
 * second) starts from its own value. Off online and in recordings (settings_no_ring_drain). */
static void no_drain(uint32_t racer) {
    static uint32_t best[8];
    static uint64_t seen[8];
    uint32_t d = racer - GAME_PLAYERS;
    if (d >= 8 * GAME_PLAYER_SIZE || d % GAME_PLAYER_SIZE) return;
    int i = (int)(d / GAME_PLAYER_SIZE);
    uint32_t pad = ld32(racer);
    if (ld8(racer + GAME_RACER_GEAR) != GAME_GEAR_CHAOS_EMERALD || ld8(racer + 0xBCu) != 0 ||
        pad < 0x801AF9F0u || pad >= 0x801AF9F0u + 4u * 0x30u) return;  /* players only (CPU flag +0xBC, a pad) */
    uint64_t now = rt_ticks();
    int32_t air = (int32_t)ld32(racer + 0x984u);
    if (now - seen[i] > 40500000ull / 2 || air < 0) best[i] = air < 0 ? 0 : (uint32_t)air;
    seen[i] = now;
    if ((uint32_t)air < best[i]) st32(racer + 0x984u, best[i]);
    else best[i] = (uint32_t)air;
}

/* Every place the game adds to a racer's step air rewards (+0x988; patches.txt "reward sources"). With Ring Replenish,
 * a player's Super Sonic is paid per kind here (the user, 2026-11-20, from a Switch race log of each site):
 *   8187BCAC grinding (state 18, +347 a frame on a fast rail): RW_GRIND_NUM/DEN of it (was 1/2: "increase a more")
 *   8187AC90 flying (state 19, grows each frame of a flight, about 260 to 830): its growth above FLY_KNEE is cut to
 *            FLY_GROWTH and the whole to RW_FLY_NUM/DEN (was 1/2: "decrease ... and its exponential gain just a bit")
 *   81879C20 / 818970D4 smashed objects and the like (10 000 / 15 000 / 20 000 / 30 000): as before, at most
 *            REWARD_MAX_RINGS rings, then REPLENISH_NUM/DEN ("at a good rate, no change")
 * Whatever reaches the fuel step that no site paid is the trick landing (lv_super_rewards). */
#define RW_GRIND_NUM 7u
#define RW_GRIND_DEN 10u
#define FLY_KNEE     260u
#define FLY_GROWTH_NUM 3u   /* 75 % of the growth above the knee */
#define FLY_GROWTH_DEN 4u
#define RW_FLY_NUM   9u     /* then 45 % of that */
#define RW_FLY_DEN   20u
#define TRICK_X_REWARD 100000u  /* the game's landing reward for an X rank */
#define TRICK_X_RINGS  75u
static uint32_t g_site_paid[8];  /* this step's rewards paid through the sites (after tuning), per racer */

static int super_player(uint32_t racer) {
    uint32_t pad = ld32(racer);
    return ld8(racer + GAME_RACER_GEAR) == GAME_GEAR_CHAOS_EMERALD && ld8(racer + 0xBCu) == 0 &&
           pad >= 0x801AF9F0u && pad < 0x801AF9F0u + 4u * 0x30u;
}

uint32_t lv_reward_site(uint32_t site, uint32_t racer, uint32_t value) {
    uint32_t d = racer - GAME_PLAYERS;
    if (d >= 8 * GAME_PLAYER_SIZE || d % GAME_PLAYER_SIZE || !super_player(racer)) return value;
    int i = (int)(d / GAME_PLAYER_SIZE);
    uint32_t old = ld32(racer + 0x988u);
    int32_t gain = (int32_t)(value - old);
    if (gain <= 0 || !settings_ring_replenish()) return value;
    uint32_t g = (uint32_t)gain, paid;
    uint32_t ring = ld32(racer + 0x8DCu + 0x1Cu) / 100u;  /* fuel per ring */
    if (site == 0x8187BCACu) {
        paid = g * RW_GRIND_NUM / RW_GRIND_DEN;
    } else if (site == 0x8187AC90u) {
        uint32_t c = g <= FLY_KNEE ? g : FLY_KNEE + (g - FLY_KNEE) * FLY_GROWTH_NUM / FLY_GROWTH_DEN;
        paid = c * RW_FLY_NUM / RW_FLY_DEN;
    } else {
        uint32_t most = ring * REWARD_MAX_RINGS;
        paid = (most && g > most ? most : g) * REPLENISH_NUM / REPLENISH_DEN;
    }
    g_site_paid[i] += paid;
    static unsigned lines;
    if (getenv("SR_BOOSTLOG") && lines < 3000) {
        lines++;
        rt_log("[reward] racer %d site %08X +%d paid %u (rings %u, state %u, anim %u)", i, site, gain, paid,
               ld32(racer + 0xB98u), ld8(racer + 0x1034u), ld32(racer + 0x764u));
    }
    return old + paid;
}

/* Super Sonic's invisible ring magnet (Riders Plus, the user 2026-11-20, with the dearer held boost). The game's
 * Magnetic Barrier (item box type 7, "draws in rings like a magnet") is f_8188CC74(racer index): it sets racer +0xBB0
 * bit 0x4 and starts task 818F0B94, which registers a collision sphere around the racer (f_81852314, touch callback
 * 818F0EA4 pulls the rings), draws the electric bubble, and ends after 600 steps (timer task +0x1C) by clearing the
 * bit. The flag alone does nothing (Switch 2 test). So: while a Super Sonic has no barrier, one is started for him
 * (lv_magnet_spawn, at his fuel step); his barrier's timer is held at 0 (lv_magnet_keep) and its drawing is skipped
 * (lv_magnet_hide). Game memory, game steps only: online and recordings stay in step (Riders Plus is synced). */
void f_8188CC74(Cpu* c);

static int magnet_racer(uint32_t task) {  /* the Super Sonic racer a Magnetic Barrier task belongs to, else 0 */
    uint32_t work = ld32(task + 0x10u);
    if (g_ring_cap == 100 || work < 0x80000000u || work >= 0x81800000u) return 0;
    uint32_t idx = ld8(work + 0x3Cu);
    if (idx >= 8) return 0;
    uint32_t racer = GAME_PLAYERS + idx * GAME_PLAYER_SIZE;
    return super_racer(racer) ? (int)racer : 0;
}

void lv_magnet_spawn(Cpu* c, uint32_t sp, uint32_t racer) {
    if (g_ring_cap == 100 || !super_racer(racer) || (ld32(racer + 0xBB0u) & 4u)) return;
    int i = racer_index(racer);
    if (i < 0) return;
    static Cpu saved;  /* game thread only */
    saved = *c;
    c->r[1] = (sp - 0x400u) & ~15u;  /* below the caller's live frame */
    c->r[3] = (uint32_t)i;
    c->lr = 0;
    f_8188CC74(c);
    *c = saved;
    if (getenv("SR_BOOSTLOG")) rt_log("[magnet] racer %d: barrier started (+BB0 %08X)", i, ld32(racer + 0xBB0u));
}

void lv_magnet_keep(uint32_t task) {
    if (magnet_racer(task)) st32(task + 0x1Cu, 0);
}

int lv_magnet_hide(uint32_t task) { return magnet_racer(task) != 0; }

/* 81815EEC: the step's air rewards (+0x988: tricks, smashed objects, flying, air boxes...) are thrown away for
 * ring-powered gear (+0x9D4 bit 0x400). With Ring Replenish, Super Sonic keeps them as fuel (his rings). */
uint32_t lv_super_rewards(uint32_t racer, uint32_t rewards, uint32_t original) {
    if (settings_no_ring_drain()) no_drain(racer);
    uint32_t d = racer - GAME_PLAYERS;
    int i = d < 8 * GAME_PLAYER_SIZE && !(d % GAME_PLAYER_SIZE) ? (int)(d / GAME_PLAYER_SIZE) : -1;
    uint32_t paid = i >= 0 ? g_site_paid[i] : 0;
    if (i >= 0) g_site_paid[i] = 0;
    if (!settings_ring_replenish() || !super_racer(racer) || (int32_t)rewards <= 0) return original;
    /* the sites' part is already tuned; the rest is a trick landing. The game's landing reward follows the rank
     * (Switch log 2026-11-20: 20 000, 30 000, 40 000, 45 000, 50 000, 60 000 and 100 000 for the top rank, X); Super
     * Sonic gets rings in proportion, TRICK_X_RINGS for X (the user: "X rank should give 75 rings"), at most that */
    uint32_t rest = rewards > paid ? rewards - paid : 0;
    uint32_t max_air = ld32(racer + 0x8DCu + 0x1Cu), ring = max_air / 100u;
    uint64_t t = (uint64_t)(rest > TRICK_X_REWARD ? TRICK_X_REWARD : rest) * TRICK_X_RINGS * ring / TRICK_X_REWARD;
    uint32_t trick = (uint32_t)t;
    if (rest && getenv("SR_BOOSTLOG"))
        rt_log("[trick] racer %d: landing reward %u (paid %u), state %u, anim %u", i, rest, trick, ld8(racer + 0x1034u),
               ld32(racer + 0x764u));
    return paid + trick;
}

/* The native ring-powered path mirrors floor(fuel * 100 / max_air) into the ring word.
 * Keep that denominator and all costs intact; only extend the fuel clamp. Raising max_air itself
 * would dilute each ring and would still leave the mirror bounded to 100. */
uint32_t lv_super_air_cap(uint32_t racer, uint32_t air, uint32_t max_air, uint32_t original) {
    if (g_ring_cap == 100 || !super_racer(racer) || !max_air) return original;
    if ((int32_t)air < 0) return 0;
    if (racer == g_gain_racer) {
        g_gain_racer = 0;
        /* a step that gained (rails, the air rewards above): only REPLENISH_NUM/DEN of the gain; off: none of the
         * rewards were kept, rails as before */
        if (settings_ring_replenish() && (int32_t)g_gain_start >= 0 && air > g_gain_start) {
            uint32_t gained = air - g_gain_start, kept = gained;  /* each part tuned at its source (lv_reward_site) */
            /* in a spin section the game's own refill (5000 a turn) also lands here: only lv_super_spin_turn pays
             * there (Switch log 2026-11-20: both paid) */
            if (ld16(racer + 0x624u) == 1) kept = 0;
            air = g_gain_start + kept;
            if (getenv("SR_BOOSTLOG") && gained >= 500)
                rt_log("[replenish] Super Sonic fuel +%u (of +%u), now %u", kept, gained, air);
        }
    }
    uint64_t cap = (uint64_t)max_air * (uint32_t)g_ring_cap / 100u;
    if (cap > INT32_MAX) cap = INT32_MAX;
    return (uint32_t)((uint64_t)air < cap ? air : cap);
}

static void restore_level(uint32_t racer, LvRacer* r) {
    if (!r->valid || !r->applied) return;
    for (int f = 0; f < 3; f++)
        if (ld32(racer + r->block + k_off[f]) == r->wrote[f])
            st32(racer + r->block + k_off[f], r->base[f]);
}

/* Before the real stat constructor, not after a new gear's baseline might equal our old raised value. */
void lv_racer_reset(uint32_t racer) {
    int i = racer_index(racer);
    if (i < 0) return;
    restore_level(racer, &g_lv[i]);
    memset(&g_lv[i], 0, sizeof g_lv[i]);
}

/* Before the game's race initialization; identical stats in consecutive races are still a new owner. */
void lv_race_reset(void) {
    for (int i = 0; i < 8; i++) restore_level(GAME_PLAYERS + (uint32_t)i * GAME_PLAYER_SIZE, &g_lv[i]);
    memset(g_lv, 0, sizeof g_lv);
}

static void write_level(uint32_t racer, LvRacer* r, int ext) {
    const float* k[3] = {k_top, k_drift, k_boost};
    int balance = super_racer(racer) && g_ring_cap != 100;
    for (int f = 0; f < 3; f++) {
        float v = u32_as_f32(r->base[f]) * (balance ? k_super[f][ext] : k[f][ext]);
        r->wrote[f] = balance || ext ? f32_as_u32(v) : r->base[f];
        st32(racer + r->block + k_off[f], r->wrote[f]);
    }
    if (ext > r->ext) r->levelup = 1;
    if (ext != r->ext) rt_log("[levels] racer %d: Lv. %d (rings %u)", racer_index(racer), 3 + ext, ld32(racer + 0xB98));
    r->ext = ext;
    r->applied = balance || ext;
    if (!ext) r->levelup = 0;
}

/* patches.txt: the per-frame racer loops, just before the game's own level update (game thread) */
void lv_racer_update(uint32_t racer) {
    int i = racer_index(racer);
    if (i < 0) return;
    LvRacer* r = &g_lv[i];
    /* Native racer/feedback hooks still run after finishing. Retire before any application,
     * using the same guest-state gate as frame retirement, or each frame rearms Lv.5 effects. */
    if (!active_race()) {
        if (r->valid) lv_racer_reset(racer);
        return;
    }
    int super = super_racer(racer);
    uint32_t block = super ? 0x8DCu : BLK3;
    uint32_t identity = ld8(racer + GAME_RACER_GEAR) | (uint32_t)ld8(racer + GAME_RACER_CHARACTER) << 8 |
                        (uint32_t)ld8(racer + 0xBFu) << 16;
    if (r->valid && (r->block != block || r->identity != identity)) {
        restore_level(racer, r);
        memset(r, 0, sizeof *r);
    }
    int changed = !r->valid;
    for (int f = 0; f < 3; f++) {
        uint32_t cur = ld32(racer + block + k_off[f]);
        /* Track fields independently: a stat mod rebuilding just boost must not turn our raised top
         * speed into the next baseline. A full race rebuild is also caught explicitly by reset. */
        if (!r->valid || !r->applied || cur != r->wrote[f]) { r->base[f] = cur; changed = 1; }
    }
    r->block = block; r->identity = identity; r->valid = 1;
    int want = 0;
    if (g_ring_cap != 100 && (super || ld8(racer + 0x102E) == 2)) {
        uint32_t rings = ld32(racer + 0xB98);
        want = rings >= LV5_RINGS ? 2 : rings >= LV4_RINGS ? 1 : 0;
    }
    int apply = want || (super && g_ring_cap != 100);
    if (want != r->ext || changed || apply != r->applied) write_level(racer, r, want);
}

/* Game thread, once per simulation step. Ring-powered racers can skip the native level checks;
 * also retire outstanding writes when toggled off, including a player whose native check is suspended. */
/* SR_SUPERDIAG=1 (investigation, 2026-11-20: what turns Super Sonic blue / gold): each step, racer 0 with the Chaos
 * Emerald gear: every byte of the racer that changes is logged for its first 3 changes this race (rarely changing
 * fields: flags, ids, model pointers), with the rings (+0xB98), the animation (+0x764) and the state (+0x1034). */
static void super_diag(void) {
    static int on = -1;
    static uint8_t snap[GAME_PLAYER_SIZE], cnt[GAME_PLAYER_SIZE];
    static int have, lines;
    static uint32_t step;
    if (on < 0) { const char* e = getenv("SR_SUPERDIAG"); on = e && atoi(e) == 1; }
    if (!on) return;
    uint32_t p = GAME_PLAYERS;
    if (!active_race() || ld8(p + GAME_RACER_GEAR) != GAME_GEAR_CHAOS_EMERALD) { have = 0; return; }
    step++;
    if (!have) {
        for (uint32_t o = 0; o < GAME_PLAYER_SIZE; o++) { snap[o] = ld8(p + o); cnt[o] = 0; }
        have = 1;
        rt_log("[superdiag] race start: char %u orig %u gear %u rings %u anim %u state %u", ld8(p + 0xBAu), ld8(p + 0xBFu),
               ld8(p + 0xBBu), ld32(p + 0xB98u), ld32(p + 0x764u), ld8(p + 0x1034u));
        return;
    }
    for (uint32_t o = 0; o < GAME_PLAYER_SIZE; o++) {
        uint8_t v = ld8(p + o);
        if (v == snap[o]) continue;
        if (cnt[o] < 3 && lines < 8000) {
            cnt[o]++;
            lines++;
            rt_log("[superdiag] step %u +0x%03X: %02X -> %02X (n%u) rings %u anim %u state %u", step, o, snap[o], v, cnt[o],
                   ld32(p + 0xB98u), ld32(p + 0x764u), ld8(p + 0x1034u));
        } else if (cnt[o] < 255) cnt[o]++;
        snap[o] = v;
    }
}

/* SR_SUPERDIAG=2 (the user's test, 2026-11-20: transform and lose it again and again in front of a ring line): racer
 * 0 with the Chaos Emerald gear; at each change of its state byte +0x1034 every byte that differs from the previous
 * change is logged, except bytes that change on most steps (positions, timers). Bytes that flip with every
 * gold <-> blue switch are the form. Also, once per race, every racer word that looks like a pointer (objects to
 * follow next). */
static void super_diag2(void) {
    /* regions: the racer, then the objects its words +0x820 / +0xA3C point to (0x800 bytes each; +0x6B8 is a track
     * path node that moves every few frames, Switch log 2026-11-20) */
    enum { NREG = 3, OBJ = 0x800, TOT = GAME_PLAYER_SIZE + 2 * OBJ };
    static const uint32_t ptr_off[NREG] = {0, 0x820u, 0xA3Cu};
    static const uint32_t reg_len[NREG] = {GAME_PLAYER_SIZE, OBJ, OBJ};
    static int on = -1;
    static uint8_t snap[TOT], prev[TOT];
    static uint16_t changes[TOT];
    static uint32_t base[NREG];
    static uint32_t step, events;
    static int have;
    if (on < 0) { const char* e = getenv("SR_SUPERDIAG"); on = e && atoi(e) == 2; }
    if (!on) return;
    uint32_t p = GAME_PLAYERS;
    if (!active_race() || ld8(p + GAME_RACER_GEAR) != GAME_GEAR_CHAOS_EMERALD) { have = 0; return; }
    for (int r = 0; r < NREG; r++) {
        uint32_t b = r ? ld32(p + ptr_off[r]) : p;
        if (b < 0x80000000u || b >= 0x81800000u - OBJ) b = 0;  /* MEM1 heap objects only */
        if (r && b != base[r]) have = 0;  /* a new object: start over */
        base[r] = r ? b : p;
    }
    if (!have) {
        uint32_t k = 0;
        for (int r = 0; r < NREG; r++)
            for (uint32_t o = 0; o < reg_len[r]; o++, k++) {
                snap[k] = prev[k] = base[r] ? ld8(base[r] + o) : 0;
                changes[k] = 0;
            }
        have = 1;
        step = 0;
        rt_log("[superdiag2] regions: racer %08X, +820 -> %08X, +A3C -> %08X", base[0], base[1], base[2]);
        return;
    }
    step++;
    uint32_t k = 0;
    for (int r = 0; r < NREG; r++)
        for (uint32_t o = 0; o < reg_len[r]; o++, k++) {
            uint8_t v = base[r] ? ld8(base[r] + o) : 0;
            if (v != prev[k]) {
                /* a byte's first change once the race has settled (10 s in): item effects such as the Magnet box's
                 * timer show up here */
                static unsigned news;
                if (!changes[k] && step > 600 && news < 600) {
                    news++;
                    rt_log("[superdiag2] new step %u %c%03X: %02X>%02X state %u anim %u rings %u", step, "RAB"[r], o, prev[k],
                           v, ld8(p + 0x1034u), ld32(p + 0x764u), ld32(p + 0xB98u));
                }
                if (changes[k] < 65535) changes[k]++;
            }
            prev[k] = v;
        }
    static uint8_t last_state = 0xFF;
    uint8_t st = ld8(p + 0x1034u);
    if (st == last_state) return;
    last_state = st;
    events++;
    char line[700];
    int n = snprintf(line, sizeof line, "[superdiag2] #%u step %u state %u anim %u rings %u:", events, step, st,
                     ld32(p + 0x764u), ld32(p + 0xB98u));
    k = 0;
    for (int r = 0; r < NREG; r++)
        for (uint32_t o = 0; o < reg_len[r]; o++, k++) {
            uint8_t v = base[r] ? ld8(base[r] + o) : 0;
            if (v != snap[k] && changes[k] * 4u < step) {  /* not a byte that changes on most steps */
                n += snprintf(line + n, sizeof line - (size_t)n, " %c%03X:%02X>%02X", "RAB"[r], o, snap[k], v);
                if (n > 640) { rt_log("%s", line); n = snprintf(line, sizeof line, "[superdiag2] #%u more:", events); }
            }
            snap[k] = v;
        }
    rt_log("%s", line);
}

void lv_racers_frame(void) {
    super_diag();
    super_diag2();
    if (!active_race()) {
        lv_race_reset();
        return;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t p = GAME_PLAYERS + (uint32_t)i * GAME_PLAYER_SIZE;
        if (super_racer(p) || g_lv[i].applied) lv_racer_update(p);
    }
}

/* Before the native check's air/ability guards. Jump to its existing effect/sound/panel branch,
 * after all native level, max-speed and air-refill writes. No synthetic native level for Super. */
int lv_super_player_update(uint32_t racer) {
    int i = racer_index(racer);
    if (i < 0 || !super_racer(racer)) return 0;
    lv_racer_update(racer);
    if (!g_lv[i].ext || !g_lv[i].levelup) return 0;
    g_lv[i].levelup = 0;
    if (getenv("SR_LEVELLOG")) rt_log("[levels] racer %d: Super level-up feedback, native %u, fuel %u refill %u",
        i, ld8(racer + 0x102E), ld32(racer + 0x984), ld32(racer + 0x988));
    return 1;
}

/* patches.txt: the player's own level-up check (fn 81836054, before it reads the byte) */
void lv_player_update(uint32_t racer) {
    lv_racer_update(racer);
    int i = racer_index(racer);
    if (i < 0 || !g_lv[i].levelup) return;
    if (super_racer(racer)) return;
    g_lv[i].levelup = 0;
    if (ld8(racer + 0x102E) == 2 && ld32(racer + 0xB98) >= 60) st8(racer + 0x102E, 1);  /* the game raises it back */
}

/* ---- HUD ---- */
/* the two digit styles: the row's "3" cell (reference), its size, and where the made "4" / "5" go */
typedef struct { int rx, ry, size, dx[2], dy; } CellKind;
static const CellKind k_kind[2] = {
    {430, 235, 20, {316, 340}, 281},  /* split screen: entries 0x86..0x89 */
    {346, 0, 16, {316, 336}, 310},    /* 1P: entries 0x11..0x14 */
};
#define CELL_TABLE 0x80001F00u  /* four sprite-table entries (16 bytes each) in unused low memory */

/* patches.txt: the HUD's level-digit cell pointer for `racer` (kind 0 = 20x20 cells, 1 = 16x16) */
uint32_t lv_hud_cell(uint32_t racer, uint32_t cell, int kind) {
    int i = racer_index(racer);
    if (i < 0 || kind < 0 || kind > 1 || g_lv[i].ext == 0) return cell;
    if (getenv("SR_LEVELLOG")) {
        static int seen[8][2];
        if (seen[i][kind] != g_lv[i].ext) {
            rt_log("[levels-hud] racer %d kind %d ext %d source %08X rect %u %u %u %u", i, kind, g_lv[i].ext,
                   cell, ld32(cell), ld32(cell + 4), ld32(cell + 8), ld32(cell + 12));
            seen[i][kind] = g_lv[i].ext;
        }
    }
    int e = g_lv[i].ext - 1;
    const CellKind* k = &k_kind[kind];
    uint32_t a = CELL_TABLE + 16u * (uint32_t)(kind * 2 + e);
    st32(a + 0, (uint32_t)k->dx[e]);
    st32(a + 4, (uint32_t)k->dy);
    st32(a + 8, (uint32_t)k->size);
    st32(a + 12, (uint32_t)k->size);
    return a;
}

/* ---- the "4" and "5" cells, drawn into the decoded HUD texture ---- */
static inline uint8_t* px(GxTexture* t, int x, int y) { return t->rgba + 4 * ((size_t)y * t->width + (size_t)x); }

static int is_fill(const uint8_t* p) {  /* the digits' white fill */
    uint8_t m = p[0] < p[1] ? p[0] : p[1];
    if (p[2] < m) m = p[2];
    return p[3] > 100 && m > 170;
}

static int is_hud_texture(GxTexture* t) {
    if (t->width != 512 || t->height != 512 || !t->rgba) return 0;
    uint32_t h = 2166136261u;  /* FNV-1a of the alpha of the level digits "0123" (cells 0x86..0x89) */
    for (int y = 235; y < 255; y++)
        for (int x = 370; x < 450; x++) h = (h ^ px(t, x, y)[3]) * 16777619u;
    if (h != 0x0435F077u) return 0;
    for (int y = 278; y < 329; y++)  /* the spots for the new cells must be empty */
        for (int x = 313; x < 363; x++)
            if (px(t, x, y)[3]) return 0;
    return 1;
}

#define SS 4  /* supersampling */
static void make_cell(GxTexture* t, const CellKind* k, int sx, int sy, int sw, int sh, int dx, int dy) {
    /* the row's reference "3" cell: its fill box, fill colour and outline colour */
    const int Z = k->size;
    int rx0 = Z, ry0 = Z, rx1 = -1, ry1 = -1;
    unsigned fr = 0, fg = 0, fb = 0, nf = 0, oa = 255;
    uint8_t oc[3] = {0, 0, 0};
    int darkest = 1 << 30;
    for (int y = 0; y < Z; y++)
        for (int x = 0; x < Z; x++) {
            const uint8_t* p = px(t, k->rx + x, k->ry + y);
            if (is_fill(p)) {
                if (x < rx0) rx0 = x;
                if (x > rx1) rx1 = x;
                if (y < ry0) ry0 = y;
                if (y > ry1) ry1 = y;
                fr += p[0]; fg += p[1]; fb += p[2]; nf++;
            } else if (p[3] > 200 && p[0] + p[1] + p[2] < darkest) {
                darkest = p[0] + p[1] + p[2];
                oc[0] = p[0]; oc[1] = p[1]; oc[2] = p[2]; oa = p[3];
            }
        }
    if (!nf || rx1 < 0) return;
    uint8_t fc[3] = {(uint8_t)(fr / nf), (uint8_t)(fg / nf), (uint8_t)(fb / nf)};
    /* the source glyph's fill box */
    int gx0 = sw, gy0 = sh, gx1 = -1, gy1 = -1;
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++)
            if (is_fill(px(t, sx + x, sy + y))) {
                if (x < gx0) gx0 = x;
                if (x > gx1) gx1 = x;
                if (y < gy0) gy0 = y;
                if (y > gy1) gy1 = y;
            }
    if (gx1 < 0) return;
    float th = (float)(ry1 - ry0 + 1), gh = (float)(gy1 - gy0 + 1), gw = (float)(gx1 - gx0 + 1);
    float s = th / gh, tw = gw * s;
    float ox = (rx0 + rx1 + 1) * 0.5f - tw * 0.5f, oy = (float)ry0;
    /* fill on a supersampled grid */
    enum { NMAX = 20 * SS };
    static uint8_t fill[NMAX][NMAX], outl[NMAX][NMAX];
    const int N = Z * SS;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = ((x + 0.5f) / SS - ox) / s + gx0, v = ((y + 0.5f) / SS - oy) / s + gy0;
            int iu = (int)floorf(u), iv = (int)floorf(v);
            fill[y][x] = iu >= gx0 && iu <= gx1 && iv >= gy0 && iv <= gy1 && is_fill(px(t, sx + iu, sy + iv));
        }
    /* round outline: the fill grown by about 2.3 pixels (at 20x20; scaled with the cell) */
    const int R = (int)(2.3f * Z / 20.0f * SS + 0.5f);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            uint8_t o = 0;
            for (int dy2 = -R; dy2 <= R && !o; dy2++)
                for (int dx2 = -R; dx2 <= R && !o; dx2++)
                    if (dx2 * dx2 + dy2 * dy2 <= R * R) {
                        int yy = y + dy2, xx = x + dx2;
                        if (yy >= 0 && yy < N && xx >= 0 && xx < N && fill[yy][xx]) o = 1;
                    }
            outl[y][x] = o;
        }
    for (int y = 0; y < Z; y++)
        for (int x = 0; x < Z; x++) {
            unsigned cf = 0, co = 0;
            for (int j = 0; j < SS; j++)
                for (int k = 0; k < SS; k++) { cf += fill[y * SS + j][x * SS + k]; co += outl[y * SS + j][x * SS + k]; }
            float af = cf / (float)(SS * SS), ao = co / (float)(SS * SS) * (oa / 255.0f);
            /* fill over outline, straight alpha */
            float a = af + ao * (1 - af);
            uint8_t* d = px(t, dx + x, dy + y);
            for (int c = 0; c < 3; c++)
                d[c] = a > 0 ? (uint8_t)((fc[c] * af + oc[c] * ao * (1 - af)) / a + 0.5f) : 0;
            d[3] = (uint8_t)(a * 255 + 0.5f);
        }
}

/* gx_texture.c: a texture was just decoded */
void lv_texture_decoded(GxTexture* t) {
    if (!is_hud_texture(t)) return;
    /* the HUD's orange 22x22 digits (entries 0x2F = "4", 0x30 = "5") */
    for (int kind = 0; kind < 2; kind++)
        for (int e = 0; e < 2; e++)
            make_cell(t, &k_kind[kind], 319 + 22 * e, 199, 22, 22, k_kind[kind].dx[e], k_kind[kind].dy);
}

/* Spin scenes (Riders Plus): each counted stick turn refills a racer's air by 5000, but Super Sonic's air is his
 * rings and the game's refill stops at his normal tank (100 rings), so he gained nothing. With Ring Replenish each turn
 * gives him SPIN_FUEL here (1600, about 1.45 rings), up to the Riders Plus ceiling (lv_super_air_cap). patches.txt: the turn is
 * counted at 818A78E8 (racer r3: +0xA04 bit 31), once per turn. */
void lv_super_spin_turn(uint32_t racer) {
    if (!settings_ring_replenish() || !super_racer(racer)) return;
    uint32_t max_air = ld32(racer + 0x8DCu + 0x1Cu), air = ld32(racer + 0x984u);
    if (!max_air || (int32_t)air < 0) return;
    uint64_t cap = (uint64_t)max_air * (uint32_t)g_ring_cap / 100u,
             now = (uint64_t)air + SPIN_FUEL;
    if (now > cap) now = cap;
    st32(racer + 0x984u, (uint32_t)now);
    if (getenv("SR_BOOSTLOG")) rt_log("[spin] Super Sonic turn: fuel %u -> %u", air, (uint32_t)now);
}

/* Charge jump (also part of the option): holding A to charge a jump keeps the racer's speed for 0.75 s before
 * the game's slowdown starts. fn_1_6D5D4 (racer r30), while racer +0xA28 bit 0x1000 (charging) is set, counts
 * the charge t at racer +0x9F4 (+1 per step, at most 50) and each step takes 0.009259 x (t/50)^2 (up to 2 km/h)
 * off the speed (+0xAAC), down to 80 km/h, with the acceleration (+0xAA8) at 0. Here the slowdown's t/50 is
 * replaced by the same curve 0.75 s later; the jump's own charge (+0x9F4) is untouched. Counted in 60ths of a
 * second (the game's frame advance), so 30 fps mode holds as long. */
#define JUMP_HOLD_60THS 45
#define GAME_FRAME_ADV  0x8021B8D4u  /* u32, as frame_pacing.c */
static uint32_t g_jump_held[8];

void jump_charge_step(uint32_t racer) {  /* every call of fn_1_6D5D4, so the count resets when a charge ends */
    int i = racer_index(racer);
    if (i < 0) return;
    if (ld32(racer + 0xA28u) & 0x1000u) g_jump_held[i] += ld32(GAME_FRAME_ADV);
    else g_jump_held[i] = 0;
}

double jump_charge_scale(uint32_t racer, double orig) {  /* t/50 for the slowdown */
    int i = racer_index(racer);
    if (g_ring_cap == 100 || i < 0) return orig;
    uint32_t adv = ld32(GAME_FRAME_ADV);
    if (adv < 1) adv = 1;
    uint32_t held = g_jump_held[i];
    double t = held > JUMP_HOLD_60THS ? (double)((held - JUMP_HOLD_60THS) / adv) : 0.0;
    if (t > 50.0) t = 50.0;
    return t / 50.0;
}

/* Starting a charge also ends a boost (+0xA28 bit 0x400 cleared), and a racer above its top speed (+0xAB4) is
 * then eased back down by the game's general decay (fn at 818159F4.., racer r24: speed -= c + k x over^2,
 * stored at 81815ABC). During the same 0.75 s of a charge that decay waits too, so a jump charged right after a
 * boost keeps the boost speed for that time. */
int jump_charge_grace(uint32_t racer) {
    int i = racer_index(racer);
    if (g_ring_cap == 100 || i < 0) return 0;
    return (ld32(racer + 0xA28u) & 0x1000u) && g_jump_held[i] <= JUMP_HOLD_60THS;
}
