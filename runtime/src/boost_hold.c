/* Hold to boost (Gameplay tab, off by default): a boost started with B (or X) lasts for as long as that button
 * stays held, nitrous style, and keeps taking air while it runs. A tap is the game's own boost, unchanged.
 *
 * How the game boosts (found with a per-frame racer dump of a user-played race, 2026-11-20): a boost sets racer
 * +0xA28 bit 0x400 (the boost state: boost speed as top speed, the boost look, boost attacks) and the timer
 * racer +0x8AC to 120, and books the gear's boost cost (the active level block +0x28: racer +0x8DC + 0x38 x
 * level byte +0x102E; 25 000 / 30 000 / 40 000 air on the gear tested) through +0x98C. While boosting, fn
 * 81896C9C (racer r31) runs once per racer per game step: timer - 1, and below 0 the boost ends. A hit or a
 * crash ends it earlier (the game clears the bit and the tick stops). Buttons: the racer's controller struct
 * (racer +0x0 -> 801AF9F0 + port x 0x30) +0x8 held bits, B 0x0002, X 0x0004.
 *
 * Here (patches.txt: in that tick, after the decrement, before the game tests it): on a boost's first tick the
 * held boost buttons are remembered. While one of them is still held and the timer has run down to HOLD_T, it
 * is put back to HOLD_T and the air for the refunded frames is taken at the gear's own rate (boost cost / 120
 * per frame: the same as tapping boosts back to back, so gears and levels keep their differences). Release, an
 * empty tank or anything that ends the boost stops it; nothing ever starts a boost, so an interrupted one does
 * not come back. Super Sonic: the same; his fuel (+0x984, rings = fuel x 100 / max air) only goes down here,
 * so the game's own ring mirror and caps stay as they are.
 *
 * Speed while held: the game sets the boost speed once at the start, and after a turn or a bump its normal
 * acceleration climbs back very slowly. A held boost pulls the speed (+0xAAC) back up to the boost speed (level
 * block +0x34) quickly instead. Super Sonic (Riders Plus): the longer it is held, the higher his boost speed and
 * the harder the pull, rising exponentially to SUPER_CAP_KMH (super_tuning.h) after SUPER_RAMP frames (top speed +0xAB4 raised with
 * it; the game puts its own back when the boost ends, fn 81896C9C). Integer curve, no libm: the same on every
 * machine in an online race.
 *
 * It changes game memory, so it runs on game steps (deterministic) and an online room or a recording uses the
 * host's / recorded setting (settings_fix_boost_hold, netplay.c). */
#include <stdint.h>
#include <stdlib.h>
#include "game_addrs.h"
#include "runtime.h"
#include "settings.h"
#include "super_tuning.h"

#define PAD_STRUCTS    0x801AF9F0u
#define PAD_STRUCT_SZ  0x30u
#define BOOST_BUTTONS  0x0006u   /* controller struct +0x8: B 0x0002, X 0x0004 */
#define R_BOOST_TIMER  0x8ACu
#define R_STATE        0xA28u
#define STATE_BOOST    0x400u
#define R_AIR          0x984u
#define R_LEVEL        0x102Eu
#define R_BLOCKS       0x8DCu    /* three 0x38-byte level blocks, +0x28 boost cost */
#define BOOST_FRAMES   120       /* the game's boost length */
#define HOLD_T         4         /* frames a held boost keeps in hand: after release it ends this much later */

#define R_SPEED        0xAACu    /* f32, x216 = km/h */
#define R_TOP          0xAB4u    /* f32: top speed (the boost speed while boosting) */
#define KMH            (1.0 / 216.0)
#define PULL           0.10      /* of the gap to the boost speed, per frame */
#define PULL_MIN       (1.5 * KMH)
#define SUPER_CAP      (SUPER_CAP_KMH * KMH)

static uint16_t g_armed[8];      /* the boost buttons that started this racer's boost and are held */
static uint32_t g_extra[8];      /* frames added to the current boost (log) */
static uint32_t g_held[8];       /* frames this boost has been held */

static float ldf(uint32_t ea) { return u32_as_f32(ld32(ea)); }
static void stf(uint32_t ea, float v) { st32(ea, f32_as_u32(v)); }

static int super_racer(uint32_t racer) {
    return ld8(racer + GAME_RACER_GEAR) == GAME_GEAR_CHAOS_EMERALD &&
           ld8(racer + GAME_RACER_CHARACTER) == GAME_CHAR_SUPER_SONIC;
}

/* 0..4096: (2^(3h/SUPER_RAMP) - 1) / 7, the power of two drawn as straight pieces (integers only) */
static uint32_t ramp(uint32_t h) {
    if (h >= SUPER_RAMP) return 4096;
    uint32_t step = SUPER_RAMP / 3, n = h / step, r = h % step;
    uint32_t p = ((4096u << n) * (step + r)) / step;  /* 4096 x 2^(h/step) */
    return (p - 4096u) / 7u;
}

/* each step of a held boost: back up to the boost speed; Super climbs towards SUPER_CAP */
static void hold_speed(int i, uint32_t racer, uint32_t blk) {
    float boost = ldf(blk + 0x34u);
    if (!(boost > 0.0f) || boost > 5.0f) return;  /* not a speed */
    double target = boost, pull = PULL;
    if (super_racer(racer) && boost < SUPER_CAP) {
        uint32_t f = ramp(g_held[i]);
        double extra = SUPER_CAP - boost;
        extra = extra * (double)f;
        extra = extra / 4096.0;
        target = boost + extra;
        pull = PULL + PULL * 2.0 * (double)f / 4096.0;
        if (ldf(racer + R_TOP) < (float)target) stf(racer + R_TOP, (float)target);
    }
    double speed = ldf(racer + R_SPEED);
    if (speed >= target) return;
    double add = (target - speed) * pull;
    if (add < PULL_MIN) add = PULL_MIN;
    speed = speed + add;
    stf(racer + R_SPEED, (float)(speed < target ? speed : target));
}

static int log_on(void) {
    static int s = -1;
    if (s < 0) { const char* e = getenv("SR_BOOSTLOG"); s = e && *e && *e != '0'; }
    return s;
}

static uint32_t held_buttons(uint32_t racer) {  /* 0 for a CPU racer (no controller struct) */
    uint32_t pad = ld32(racer);
    if (pad < PAD_STRUCTS || pad >= PAD_STRUCTS + 4 * PAD_STRUCT_SZ || (pad - PAD_STRUCTS) % PAD_STRUCT_SZ) return 0;
    return ld32(pad + 0x8u);
}

/* Speed view (the user, 2026-11-20, "like some Need for Speed games"): while Super Sonic holds a boost the field of
 * view widens with his climb towards SUPER_CAP, up to FOV_BOOST_MAX. When the held boost ends it goes straight back to
 * the player's own field of view on a smooth curve over FOV_OUT_FRAMES (the user: "smoothly and immediately"). The
 * camera is shared by every screen, so only with one local player (split screen: off). Picture only: safe online. */
#define FOV_BOOST_MAX  0.18      /* +18 %, reached within about 0.4 s of holding (the user, 2026-11-20: the 4 s
                                    * climb was "waaaaay too slow") */
#define FOV_BOOST_PAST 0.04      /* a little more on top while his speed climbs past the boost speed to SUPER_CAP */
#define FOV_EASE_IN    0.12      /* share of the gap closed per frame while widening */
#define FOV_OUT_FRAMES 15        /* the way back: 0.25 s at 60 fps */
static double g_fov_target;      /* in FOV_BOOST_MAX units: 1 plus the climb's share of FOV_BOOST_PAST */
static double g_fov_climb;       /* 0..1: the climb towards SUPER_CAP */
static double g_fov_now, g_fov_from;  /* now; the value the way back started from */
static int g_fov_owner = -1;     /* the racer whose held boost widens the view, -1 = none (on the way back) */
static uint32_t g_fov_frame, g_fov_tick, g_fov_out;  /* boost_hold_fov calls; at the last tick; frames back */

/* a player's racer: a controller struct and the CPU flag +0xBC clear (racers 1-3 have controller structs in a
 * 1-player race too, driven by the CPU with +0xBC = 1; race setup 818EE774 / 818EE7CC) */
static int human(uint32_t racer) {
    uint32_t pad = ld32(racer);
    return pad >= PAD_STRUCTS && pad < PAD_STRUCTS + 4 * PAD_STRUCT_SZ && !((pad - PAD_STRUCTS) % PAD_STRUCT_SZ) &&
           ld8(racer + 0xBCu) == 0;
}

static void fov_tick(int i, uint32_t racer) {
    if (!super_racer(racer) || !human(racer)) return;
    for (int j = 0; j < 8; j++)
        if (j != i && human(GAME_PLAYERS + (uint32_t)j * GAME_PLAYER_SIZE)) return;  /* split screen */
    g_fov_owner = i;
    g_fov_climb = (double)ramp(g_held[i]) / 4096.0;
    g_fov_target = 1.0 + g_fov_climb * (FOV_BOOST_PAST / FOV_BOOST_MAX);
    g_fov_tick = g_fov_frame;
}

static void fov_release(int i) {  /* the held boost ended: back at once */
    if (g_fov_owner == i) g_fov_owner = -1;
}

float boost_hold_fov(void) {
    g_fov_frame++;
    if (g_fov_owner >= 0 && g_fov_frame - g_fov_tick > 4) g_fov_owner = -1;  /* no ticks (race over, paused) */
    if (g_fov_owner >= 0) {
        g_fov_now += (g_fov_target - g_fov_now) * FOV_EASE_IN;
        g_fov_from = g_fov_now;
        g_fov_out = 0;
    } else if (g_fov_now > 0.0) {
        g_fov_out++;
        double t = (double)g_fov_out / FOV_OUT_FRAMES;  /* smoothstep from g_fov_from down to 0 */
        g_fov_now = t >= 1.0 ? 0.0 : g_fov_from * (1.0 - t * t * (3.0 - 2.0 * t));
    }
    return (float)(1.0 + FOV_BOOST_MAX * g_fov_now);
}

/* the sound (the user, 2026-11-20: "immediately rather than gradually, like in NFS"): full at once while a held
 * Super boost goes on (80 %, up to 100 % with the climb), nothing the moment it ends; audio_out.c smooths the steps.
 * Off since the user's test the same day ("I don't like the sound muffle", another approach later): only with
 * SR_BOOSTSOUND=1 for experiments. */
double boost_hold_audio(void) {
    static int on = -1;
    if (on < 0) { const char* e = getenv("SR_BOOSTSOUND"); on = e && *e && *e != '0'; }
    return on && g_fov_owner >= 0 ? 0.8 + 0.2 * g_fov_climb : 0.0;
}

static void stop(int i, uint32_t racer, const char* why) {
    if (log_on() && g_extra[i])
        rt_log("[boost] racer %d: held boost ended (%s), %u frames past the normal boost, air %d, speed %d km/h", i,
               why, g_extra[i], (int)(int32_t)ld32(racer + R_AIR), (int)(ldf(racer + R_SPEED) * 216.0f));
    g_armed[i] = 0;
    fov_release(i);
    g_extra[i] = 0;
}

void boost_hold_tick(uint32_t racer) {
    uint32_t d = racer - GAME_PLAYERS;
    if (d >= 8 * GAME_PLAYER_SIZE || d % GAME_PLAYER_SIZE) return;
    int i = (int)(d / GAME_PLAYER_SIZE);
    int32_t t = (int32_t)ld32(racer + R_BOOST_TIMER);  /* already counted down this step */
    if (t == BOOST_FRAMES - 1) {  /* a new boost's first tick */
        g_extra[i] = 0;
        g_held[i] = 0;
        g_armed[i] = settings_boost_hold() ? (uint16_t)(held_buttons(racer) & BOOST_BUTTONS) : 0;
        if (log_on() && g_armed[i])
            rt_log("[boost] racer %d: boost with button %x held, level %u, cost %u, air %d", i, g_armed[i],
                   ld8(racer + R_LEVEL), ld32(racer + R_BLOCKS + 0x38u * (ld8(racer + R_LEVEL) > 2 ? 2u : ld8(racer + R_LEVEL)) + 0x28u),
                   (int)(int32_t)ld32(racer + R_AIR));
        return;
    }
    if (!g_armed[i]) return;
    if (!settings_boost_hold()) { stop(i, racer, "option off"); return; }
    if (!(held_buttons(racer) & g_armed[i])) { stop(i, racer, "released"); return; }
    if (!(ld32(racer + R_STATE) & STATE_BOOST)) { stop(i, racer, "boost over"); return; }
    uint32_t lv = ld8(racer + R_LEVEL);
    if (lv > 2) lv = 2;
    uint32_t blk = racer + R_BLOCKS + 0x38u * lv;
    g_held[i]++;
    hold_speed(i, racer, blk);
    fov_tick(i, racer);
    if (t >= HOLD_T) return;
    uint32_t cost = ld32(blk + 0x28u);
    if (cost == 0 || cost > 10000000u) { stop(i, racer, "no boost cost"); return; }
    uint32_t frames = (uint32_t)(HOLD_T - (t < -1 ? -1 : t));
    uint32_t need = (uint32_t)(((uint64_t)frames * cost + BOOST_FRAMES - 1) / BOOST_FRAMES);  /* rounded up */
    if (super_racer(racer)) need = (uint32_t)((uint64_t)need * SUPER_HOLD_COST_NUM / SUPER_HOLD_COST_DEN);
    int32_t air = (int32_t)ld32(racer + R_AIR);
    if (air <= 0 || (uint32_t)air < need) { stop(i, racer, "out of air"); return; }
    st32(racer + R_AIR, (uint32_t)(air - (int32_t)need));
    st32(racer + R_BOOST_TIMER, HOLD_T);
    g_extra[i] += frames;
}
