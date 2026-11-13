/* Gear levels 4 and 5 (part of the "Riders Plus" option, formerly "Ring Limit 999": settings.c g_ring_cap != 100).
 *
 * The game keeps a racer's level in a byte at racer +0x102E (0..2 = Lv. 1..3), raised when the rings reach
 * 30 / 60 (byte table at REL rodata 0x19FC) and lowered below them, and three per-level stat blocks of
 * 0x38 bytes at racer +0x8DC / +0x914 / +0x94C (built by fn 818F1F4C from the character and the gear:
 * +0x08 top speed, +0x30 drift-dash speed, +0x34 boost speed; x216 = the speedometer). About 100 functions
 * index those blocks by the level byte and the racer's air sits right after the third block, so the byte
 * never goes past 2 here. Instead, while it is 2, Lv. 4 / Lv. 5 are kept in this file and the third
 * block's speeds are raised; they are put back when the racer drops below 200 rings again.
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
#include <string.h>
#include "game_addrs.h"
#include "gx_render.h"
#include "runtime.h"

extern volatile int g_ring_cap;

#define LV4_RINGS 200
#define LV5_RINGS 350
/* each gear's own Lv. 3 values times these (Blue Star: top speed 180 -> 200 -> 215, boost 250 -> 275 -> 300). User
   2026-11-13: Lv. 5 at 350 rings (500 was near impossible on the Switch), both levels a little weaker in return;
   2026-11-14: still too fast, a little weaker again */
static const float k_top[3]   = {1.0f, 200.0f / 180.0f, 215.0f / 180.0f};
static const float k_boost[3] = {1.0f, 1.10f, 1.20f};
static const float k_drift[3] = {1.0f, 1.10f, 1.25f};

#define BLK3 0x94Cu  /* the third (Lv. 3) stat block */
#define OFF_TOP 0x08u
#define OFF_DRIFT 0x30u
#define OFF_BOOST 0x34u

typedef struct {
    int ext;                     /* 0 = the game's level, 1 = Lv. 4, 2 = Lv. 5 */
    int levelup;                 /* a level-up to play (lv_player_update) */
    uint32_t base[3];            /* the game's Lv. 3 values (top, drift, boost) as float bits */
    uint32_t wrote[3];           /* what was written, to notice the game rebuilding the block */
} LvRacer;
static LvRacer g_lv[8];
static const uint32_t k_off[3] = {OFF_TOP, OFF_DRIFT, OFF_BOOST};

static int racer_index(uint32_t racer) {
    uint32_t d = racer - GAME_PLAYERS;
    if (d >= 8 * 0x1080u || d % 0x1080u) return -1;
    return (int)(d / 0x1080u);
}

static void write_level(uint32_t racer, LvRacer* r, int ext) {
    const float* k[3] = {k_top, k_drift, k_boost};
    for (int f = 0; f < 3; f++) {
        float v = u32_as_f32(r->base[f]) * k[f][ext];
        r->wrote[f] = ext ? f32_as_u32(v) : r->base[f];
        st32(racer + BLK3 + k_off[f], r->wrote[f]);
    }
    if (ext > r->ext) r->levelup = 1;
    if (ext != r->ext) rt_log("[levels] racer %d: Lv. %d (rings %u)", racer_index(racer), 3 + ext, ld32(racer + 0xB98));
    r->ext = ext;
}

/* patches.txt: the per-frame racer loops, just before the game's own level update (game thread) */
void lv_racer_update(uint32_t racer) {
    int i = racer_index(racer);
    if (i < 0) return;
    LvRacer* r = &g_lv[i];
    uint32_t cur[3];
    for (int f = 0; f < 3; f++) cur[f] = ld32(racer + BLK3 + k_off[f]);
    if (r->ext == 0 || memcmp(cur, r->wrote, sizeof cur) != 0) {
        /* not raised by us, or the game rebuilt the block (a new race): these are its own values */
        memcpy(r->base, cur, sizeof cur);
        memcpy(r->wrote, cur, sizeof cur);
        r->ext = 0;
        r->levelup = 0;
    }
    int want = 0;
    if (g_ring_cap != 100 && ld8(racer + 0x102E) == 2) {
        uint32_t rings = ld32(racer + 0xB98);
        want = rings >= LV5_RINGS ? 2 : rings >= LV4_RINGS ? 1 : 0;
    }
    if (want != r->ext) write_level(racer, r, want);
}

/* patches.txt: the player's own level-up check (fn 81836054, before it reads the byte) */
void lv_player_update(uint32_t racer) {
    lv_racer_update(racer);
    int i = racer_index(racer);
    if (i < 0 || !g_lv[i].levelup) return;
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
    if (i < 0 || g_lv[i].ext == 0) return cell;
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
