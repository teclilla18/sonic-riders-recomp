/* Guest side of the renderer: decodes draws and EFB copies from the GX command stream into the
 * current frame's command list (gx_render.h). Runs on the guest thread that writes the FIFO. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gx.h"
#include "game_addrs.h"
#include "gx_render.h"
#include "settings.h"
#include "overlay.h"
#include "runtime.h"

void gx_texture_load_tlut(void);
int gx_texture_lookup(int map);
int gx_texture_copy_target(uint32_t addr, uint32_t w, uint32_t h);
int gx_texture_live(void);
void gx_texture_decode_stats(unsigned long long* count, unsigned long long* bytes);
void gx_texture_new_frame(void);

static GxFrame* g_frame;
static int g_state_dirty = 1;
static unsigned long long g_frames;

void gx_state_dirty(void) { g_state_dirty = 1; }

static void* grow(void* p, uint32_t* cap, uint32_t need, size_t elem) {
    if (need <= *cap) return p;
    uint32_t n = *cap ? *cap : 256;
    while (n < need) n *= 2;
    p = realloc(p, n * elem);
    if (!p) rt_fatal("out of memory (frame)");
    *cap = n;
    return p;
}

/* host time (timebase ticks) for the [perf] breakdown */
static uint64_t g_wait_ticks, g_tex_ticks;
static uint64_t g_wait_total, g_tex_total;  /* never reset ([hitch] deltas) */
static unsigned long long g_verts_total;  /* vertices emitted (after primitive expansion) */
static uint64_t g_draw_total;              /* time in gx_draw (decode, state, lookups) */
static unsigned long long g_feat_ind, g_feat_fog, g_feat_ztex;  /* draws using unimplemented features */
static unsigned long long g_feat_copies, g_feat_copy_z;            /* EFB copies to textures, from depth */
static uint32_t g_feat_copy_fmts;                                   /* their target formats seen (bit per code) */

static GxFrame* frame(void) {
    if (!g_frame) {
        uint64_t t = rt_ticks();
        g_frame = render_take_free_frame();
        { uint64_t d = rt_ticks() - t; g_wait_ticks += d; g_wait_total += d; }
        g_frame->ncmds = g_frame->nverts = g_frame->nunis = g_frame->nretired = g_frame->nxchunks = g_frame->nxtabs = 0;
        g_frame->nidx = 0;
        g_gx.regs_dirty = 1;
        g_gx.xfm_dirty = ~0u;  /* every chunk is copied into this frame's pool once */
        g_state_dirty = 1;
        gx_texture_new_frame();
    }
    return g_frame;
}

void gx_frame_retire(int id) {
    GxFrame* f = frame();
    f->retired = grow(f->retired, &f->cap_retired, f->nretired + 1, sizeof(int));
    f->retired[f->nretired++] = id;
}

static GxCmd* push_cmd(int type) {
    GxFrame* f = frame();
    f->cmds = grow(f->cmds, &f->cap_cmds, f->ncmds + 1, sizeof(GxCmd));
    GxCmd* c = &f->cmds[f->ncmds++];
    memset(c, 0, sizeof *c);
    c->type = (uint8_t)type;
    return c;
}

/* a state snapshot: copies of the XF memory chunks changed since the last one, a new chunk table
 * when any did, and a new register block only when a BP or XF register was written and differs.
 * A STATE command is pushed only when either changed. */
static void push_state(void) {
    GxFrame* f = frame();
    static uint32_t xtab[32];
    uint32_t dirty = g_gx.xfm_dirty & ((1u << GX_XFM_CHUNKS) - 1);
    for (int i = 0; i < GX_XFM_CHUNKS; i++) {
        if (!(dirty & (1u << i))) continue;
        f->xchunks = grow(f->xchunks, &f->cap_xchunks, (f->nxchunks + 1) * GX_XFM_CHUNK, sizeof(uint32_t));
        memcpy(f->xchunks + (size_t)f->nxchunks * GX_XFM_CHUNK, g_gx.xf_mem + i * GX_XFM_CHUNK, GX_XFM_CHUNK * 4);
        xtab[i] = f->nxchunks++;
    }
    g_gx.xfm_dirty = 0;
    g_state_dirty = 0;
    int new_tab = dirty || !f->nxtabs;
    if (new_tab) {
        f->xtabs = grow(f->xtabs, &f->cap_xtabs, f->nxtabs + 1, sizeof(GxXTab));
        memcpy(f->xtabs[f->nxtabs++].xtab, xtab, sizeof xtab);
    }
    int new_regs = 0;
    if (g_gx.regs_dirty) {
        g_gx.regs_dirty = 0;
        const GxUniforms* p = f->nunis ? &f->unis[f->nunis - 1] : NULL;
        if (!p || memcmp(p->bp, g_gx.bp, sizeof p->bp) || memcmp(p->xfr, g_gx.xf_reg, sizeof p->xfr) ||
            memcmp(p->konst, g_gx.konst, sizeof p->konst)) {
            f->unis = grow(f->unis, &f->cap_unis, f->nunis + 1, sizeof(GxUniforms));
            GxUniforms* u = &f->unis[f->nunis++];
            memcpy(u->bp, g_gx.bp, sizeof u->bp);
            memcpy(u->xfr, g_gx.xf_reg, sizeof u->xfr);
            memcpy(u->konst, g_gx.konst, sizeof u->konst);
            new_regs = 1;
        }
    }
    if (!new_tab && !new_regs) return;
    GxCmd* c = push_cmd(GXC_STATE);
    c->a = f->nunis - 1;
    c->b = f->nxtabs - 1;
}

/* ---- vertex decoding ---- */
static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static float comp(const uint8_t** pp, int fmt, float scale) {
    const uint8_t* p = *pp;
    float v;
    switch (fmt) {
    case 0: v = p[0]; *pp += 1; break;
    case 1: v = (int8_t)p[0]; *pp += 1; break;
    case 2: v = rd16(p); *pp += 2; break;
    case 3: v = (int16_t)rd16(p); *pp += 2; break;
    default: { uint32_t u = rd32(p); memcpy(&v, &u, 4); *pp += 4; return v; }
    }
    return v * scale;
}

static void color(const uint8_t* p, int fmt, float* o) {
    uint32_t r, g, b, a = 255;
    switch (fmt) {
    case 0: { uint16_t v = rd16(p); r = (v >> 11) * 255 / 31; g = ((v >> 5) & 63) * 255 / 63; b = (v & 31) * 255 / 31; break; }
    case 1: case 2: r = p[0]; g = p[1]; b = p[2]; break;
    case 3: { uint16_t v = rd16(p); r = (v >> 12) * 17; g = ((v >> 8) & 15) * 17; b = ((v >> 4) & 15) * 17; a = (v & 15) * 17; break; }
    case 4: { uint32_t v = (uint32_t)p[0] << 16 | p[1] << 8 | p[2];
              r = ((v >> 18) & 63) * 255 / 63; g = ((v >> 12) & 63) * 255 / 63; b = ((v >> 6) & 63) * 255 / 63; a = (v & 63) * 255 / 63; break; }
    default: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
    }
    o[0] = r / 255.0f; o[1] = g / 255.0f; o[2] = b / 255.0f; o[3] = a / 255.0f;
}

static const int k_csize[8] = {1, 1, 2, 2, 4, 0, 0, 0};
static const int k_colsize[8] = {2, 3, 4, 2, 3, 4, 0, 0};

/* Vertex decoding: everything derived from the CP registers (VCD, VAT, array bases, matrix indices)
 * is worked out once per draw, as it cannot change inside one; every output field is written
 * explicitly. Attribute data is direct (in the stream) or indexed into an array. */
typedef struct DAttr { int type, fmt, n; float scale; uint32_t base, stride; } DAttr;
typedef struct DecodePlan {
    float pnmtx, tmtx[8];       /* defaults from MATINDEX A/B */
    int pnmtx_direct, tmtx_direct[8];
    DAttr pos, nrm, col[2], tex[8];
    int nrm_direct_size, nrm_idx_extra;  /* NBT: the binormal/tangent bytes to skip */
} DecodePlan;

static void dattr(DAttr* d, int type, int arr) {
    d->type = type;
    d->base = g_gx.cp[0xA0 + arr] & 0x03FFFFFF;
    d->stride = g_gx.cp[0xB0 + arr] & 0xFF;
}

static void plan_init(DecodePlan* pl, int vat) {
    uint32_t lo = g_gx.cp[0x50], hi = g_gx.cp[0x60];
    uint32_t a = g_gx.cp[0x70 + vat], b = g_gx.cp[0x80 + vat], cc = g_gx.cp[0x90 + vat];
    uint32_t mia = g_gx.cp[0x30], mib = g_gx.cp[0x40];
    pl->pnmtx = (float)(mia & 63);
    pl->pnmtx_direct = lo & 1;
    for (int i = 0; i < 8; i++) {
        pl->tmtx[i] = (float)(i < 4 ? (mia >> (6 + 6 * i)) & 63 : (mib >> (6 * (i - 4))) & 63);
        pl->tmtx_direct[i] = (lo >> (1 + i)) & 1;
    }
    dattr(&pl->pos, (lo >> 9) & 3, 0);
    pl->pos.fmt = (a >> 1) & 7;
    pl->pos.n = (a & 1) ? 3 : 2;
    pl->pos.scale = 1.0f / (float)(1u << ((a >> 4) & 31));
    dattr(&pl->nrm, (lo >> 11) & 3, 1);
    pl->nrm.fmt = (a >> 10) & 7;
    pl->nrm.n = 3;
    pl->nrm.scale = pl->nrm.fmt == 1 ? 1.0f / 64 : pl->nrm.fmt == 3 ? 1.0f / 16384 : 1.0f;
    int nbt = (a >> 9) & 1, three_idx = nbt && ((a >> 31) & 1);
    pl->nrm_direct_size = (nbt ? 9 : 3) * k_csize[pl->nrm.fmt];
    pl->nrm_idx_extra = three_idx ? 2 * (pl->nrm.type == 2 ? 1 : 2) : 0;
    for (int c = 0; c < 2; c++) {
        dattr(&pl->col[c], (lo >> (13 + 2 * c)) & 3, 2 + c);
        pl->col[c].fmt = c == 0 ? (a >> 14) & 7 : (a >> 18) & 7;
    }
    static const struct { int word, cnt, fmt, shft; } tex[8] = {
        {0, 21, 22, 25}, {1, 0, 1, 4}, {1, 9, 10, 13}, {1, 18, 19, 22}, {1, 27, 28, -1}, {2, 5, 6, 9}, {2, 14, 15, 18}, {2, 23, 24, 27}};
    for (int i = 0; i < 8; i++) {
        dattr(&pl->tex[i], (hi >> (2 * i)) & 3, 4 + i);
        uint32_t w = tex[i].word == 0 ? a : tex[i].word == 1 ? b : cc;
        pl->tex[i].fmt = (w >> tex[i].fmt) & 7;
        pl->tex[i].n = ((w >> tex[i].cnt) & 1) ? 2 : 1;
        int shft = tex[i].shft >= 0 ? (int)((w >> tex[i].shft) & 31) : (int)(cc & 31);  /* TEX4SHFT is in VAT C */
        pl->tex[i].scale = 1.0f / (float)(1u << shft);
    }
}

static inline const uint8_t* pattr(const uint8_t** pp, const DAttr* d) {
    if (d->type == 1) return *pp;
    uint32_t idx = d->type == 2 ? (*pp)[0] : rd16(*pp);
    *pp += d->type == 2 ? 1 : 2;
    return mem_phys(d->base + idx * d->stride);
}

static void decode_planned(const uint8_t** pp, const DecodePlan* pl, GxVertex* v) {
    const uint8_t* p = *pp;
    v->pos[3] = pl->pnmtx_direct ? (float)*p++ : pl->pnmtx;
    for (int i = 0; i < 8; i++) v->tmtx[i] = pl->tmtx_direct[i] ? (float)*p++ : pl->tmtx[i];
    v->pos[0] = v->pos[1] = v->pos[2] = 0.0f;
    if (pl->pos.type) {
        const uint8_t* q = pattr(&p, &pl->pos);
        for (int k = 0; k < pl->pos.n; k++) v->pos[k] = comp(&q, pl->pos.fmt, pl->pos.scale);
        if (pl->pos.type == 1) p += pl->pos.n * k_csize[pl->pos.fmt];
    }
    v->nrm[0] = v->nrm[1] = v->nrm[2] = v->nrm[3] = 0.0f;
    if (pl->nrm.type) {
        const uint8_t* q = pattr(&p, &pl->nrm);
        for (int k = 0; k < 3; k++) v->nrm[k] = comp(&q, pl->nrm.fmt, pl->nrm.scale);
        if (pl->nrm.type == 1) p += pl->nrm_direct_size;
        else p += pl->nrm_idx_extra;
    }
    for (int c = 0; c < 2; c++) {
        if (!pl->col[c].type) { v->col[c][0] = v->col[c][1] = v->col[c][2] = v->col[c][3] = 1.0f; continue; }
        const uint8_t* q = pattr(&p, &pl->col[c]);
        color(q, pl->col[c].fmt, v->col[c]);
        if (pl->col[c].type == 1) p += k_colsize[pl->col[c].fmt];
    }
    for (int i = 0; i < 8; i++) {
        v->tex[i][0] = v->tex[i][1] = 0.0f;
        const DAttr* d = &pl->tex[i];
        if (!d->type) continue;
        const uint8_t* q = pattr(&p, d);
        for (int k = 0; k < d->n; k++) v->tex[i][k] = comp(&q, d->fmt, d->scale);
        if (d->type == 1) p += d->n * k_csize[d->fmt];
    }
    *pp = p;
}

/* the texture bound to `map` for this draw (timed for [perf] and [hitch]) */
static void lookup_map(GxCmd* c, int map) {
    uint64_t t = rt_ticks();
    c->tex[map] = gx_texture_lookup(map);
    uint64_t d = rt_ticks() - t;
    g_tex_ticks += d;
    g_tex_total += d;
}

static void draw_impl(int prim, int vat, uint32_t count, const uint8_t* vertices);
static void draw(int prim, int vat, uint32_t count, const uint8_t* vertices, uint32_t vertex_size) {
    (void)vertex_size;
    if (gx_async()) {  /* on the GP thread: the profiler's variable is the game core's */
        draw_impl(prim, vat, count, vertices);
        return;
    }
    uint32_t prev = g_ppc_cur_fn;
    g_ppc_cur_fn = PROF_DRAW;
    draw_impl(prim, vat, count, vertices);
    g_ppc_cur_fn = prev;
}
static void draw_impl(int prim, int vat, uint32_t count, const uint8_t* vertices) {
    if (!count) return;
    if (g_state_dirty) push_state();
    GxFrame* f = frame();
    /* decode each vertex once into the frame; quads, strips and fans become index lists */
    uint32_t out_n, kind;
    switch (prim) {
    case 0: case 1: out_n = count / 4 * 6; kind = GXP_TRIANGLES; break;  /* quads */
    case 2: out_n = count / 3 * 3; kind = GXP_TRIANGLES; break;
    case 3: case 4: out_n = count >= 3 ? (count - 2) * 3 : 0; kind = GXP_TRIANGLES; break;
    case 5: out_n = count / 2 * 2; kind = GXP_LINES; break;
    case 6: out_n = count >= 2 ? (count - 1) * 2 : 0; kind = GXP_LINES; break;
    default: out_n = count; kind = GXP_POINTS; break;
    }
    if (!out_n) return;
    f->verts = grow(f->verts, &f->cap_verts, f->nverts + count, sizeof(GxVertex));
    const uint8_t* p = vertices;
    DecodePlan plan;
    plan_init(&plan, vat);
    for (uint32_t i = 0; i < count; i++) decode_planned(&p, &plan, &f->verts[f->nverts + i]);
    f->idx = grow(f->idx, &f->cap_idx, f->nidx + out_n, sizeof(uint32_t));
    uint32_t* o = f->idx + f->nidx;
    const uint32_t v0 = f->nverts;
    uint32_t k = 0;
    switch (prim) {
    case 0: case 1:
        for (uint32_t i = 0; i + 3 < count; i += 4) {
            o[k++] = v0 + i; o[k++] = v0 + i + 1; o[k++] = v0 + i + 2;
            o[k++] = v0 + i; o[k++] = v0 + i + 2; o[k++] = v0 + i + 3;
        }
        break;
    case 3:
        for (uint32_t i = 2; i < count; i++) {
            if (i & 1) { o[k++] = v0 + i - 1; o[k++] = v0 + i - 2; o[k++] = v0 + i; }
            else { o[k++] = v0 + i - 2; o[k++] = v0 + i - 1; o[k++] = v0 + i; }
        }
        break;
    case 4:
        for (uint32_t i = 2; i < count; i++) { o[k++] = v0; o[k++] = v0 + i - 1; o[k++] = v0 + i; }
        break;
    case 6:
        for (uint32_t i = 1; i < count; i++) { o[k++] = v0 + i - 1; o[k++] = v0 + i; }
        break;
    default:
        for (uint32_t i = 0; i < out_n; i++) o[k++] = v0 + i;
        break;
    }
    /* unimplemented features in use, for the [perf] line: indirect stages (genmode bits 16-18),
     * fog (BP 0xEE bits 21-23), Z textures (BP 0xF4 bits 2-3) */
    if ((g_gx.bp[0x00] >> 16) & 7) g_feat_ind++;
    if ((g_gx.bp[0xEE] >> 21) & 7) g_feat_fog++;
    if ((g_gx.bp[0xF4] >> 2) & 3) g_feat_ztex++;
    GxCmd* c = push_cmd(GXC_DRAW);
    c->prim = (uint8_t)kind;
    c->a = f->nidx;
    c->b = k;
    /* textures used by the enabled TEV stages */
    for (int i = 0; i < 8; i++) c->tex[i] = -1;
    int stages = (int)((g_gx.bp[0x00] >> 10) & 15) + 1;
    for (int s = 0; s < stages; s++) {
        uint32_t order = g_gx.bp[0x28 + s / 2] >> ((s & 1) ? 12 : 0);
        int map = (int)(order & 7);
        if ((order >> 6) & 1 && c->tex[map] < 0) lookup_map(c, map);
    }
    /* the indirect stages' textures (RAS1_IREF) */
    for (int k = 0; k < (int)((g_gx.bp[0x00] >> 16) & 7) && k < 4; k++) {
        int map = (int)((g_gx.bp[0x27] >> (6 * k)) & 7);
        if (c->tex[map] < 0) lookup_map(c, map);
    }
    f->nverts += count;
    f->nidx += k;
    g_verts_total += count;
}

/* BP 0x52: copy the EFB (or a part) to an XFB or a texture, optionally clearing it */
/* draws: decoded on the thread writing the GX FIFO (the game's), timed for [hitch] and [perf] */
void gx_draw(int prim, int vat, uint32_t count, const uint8_t* vertices, uint32_t vertex_size) {
    uint64_t t = rt_ticks();
    draw(prim, vat, count, vertices, vertex_size);
    g_draw_total += rt_ticks() - t;
}

static void efb_copy(uint32_t v) {
    uint32_t tl = g_gx.bp[0x49], wh = g_gx.bp[0x4A];
    uint32_t x = tl & 0x3FF, y = (tl >> 10) & 0x3FF, w = (wh & 0x3FF) + 1, h = ((wh >> 10) & 0x3FF) + 1;
    uint32_t dest = (g_gx.bp[0x4B] & 0xFFFFFF) << 5;
    int to_xfb = (v >> 14) & 1, clear = (v >> 11) & 1, half = (v >> 9) & 1;
    if (!to_xfb) {  /* render-to-texture copies, for the [perf] line: from depth, and target formats */
        g_feat_copies++;
        if ((g_gx.bp[0x43] & 7) == 3) g_feat_copy_z++;
        g_feat_copy_fmts |= 1u << (((v >> 4) & 7) | ((v >> 3) & 1) << 3);
    }
    if (g_state_dirty) push_state();
    GxCmd* c = push_cmd(GXC_COPY);
    c->copy[0] = x; c->copy[1] = y; c->copy[2] = w; c->copy[3] = h;
    c->copy[4] = to_xfb; c->copy[5] = clear;
    c->copy[6] = (g_gx.bp[0x4F] & 0xFFFF) << 16 | (g_gx.bp[0x50] & 0xFFFF);  /* A R G B */
    c->copy[7] = g_gx.bp[0x51] & 0xFFFFFF;
    c->copy[8] = to_xfb ? 0xFFFFFFFFu
                        : (uint32_t)gx_texture_copy_target(dest, half ? w / 2 : w, half ? h / 2 : h);
    c->copy[9] = v;
    c->copy[10] = dest;
    if (to_xfb) {
        /* the Chaos Emerald gear unlocked (settings.h): kept set once a frame, as the save loads */
        if (g_settings.chaos_emerald) {
            uint8_t* st = mem_phys((GAME_GEAR_STATUS + GAME_GEAR_CHAOS_EMERALD) & 0x3FFFFFFF);
            if (*st == 0) *st = 2;
        }
        push_cmd(GXC_PRESENT);
        uint32_t ndraws = 0;
        for (uint32_t i = 0; i < g_frame->ncmds; i++) ndraws += g_frame->cmds[i].type == GXC_DRAW;
        uint64_t ts = rt_ticks();
        render_submit(g_frame);
        { uint64_t d = rt_ticks() - ts; g_wait_ticks += d; g_wait_total += d; }
        g_frame = NULL;
        g_frames++;
        /* [hitch]: a presented frame that took over 50 ms, and what the game thread spent it on */
        {
            static uint64_t last, tex0, wait0, disc_t0, draw0, idle0;
            static unsigned long long disc_b0, dec0, decb0;
            static unsigned card0;
            uint64_t now = rt_ticks();
            unsigned long long dec, decb;
            gx_texture_decode_stats(&dec, &decb);
            if (last && now - last > 40500000ull / 20) {
                double ms = 1000.0 / 40500000.0;
                double total = (now - last) * ms, disc = (di_read_ticks() - disc_t0) * ms, tex = (g_tex_total - tex0) * ms;
                double wait = (g_wait_total - wait0) * ms;
                double drw = (g_draw_total - draw0) * ms - tex, idle = (sched_idle_total() - idle0) * ms;
                rt_log("[hitch] frame %u took %.0f ms: game code %.0f ms, guest idle %.0f ms, GX draws %.1f ms, disc %.1f ms "
                       "(%llu KB), %llu new textures (%llu KB RGBA, lookups %.1f ms), waiting for the render thread %.1f ms, "
                       "card file writes %u", g_frames, total, total - disc - tex - wait - drw - idle, idle, drw, disc,
                       (di_bytes_read() - disc_b0) >> 10, dec - dec0, (decb - decb0) >> 10, tex, wait,
                       exi_card_file_writes() - card0);
            }
            last = now;
            tex0 = g_tex_total;
            wait0 = g_wait_total;
            draw0 = g_draw_total;
            idle0 = sched_idle_total();
            disc_t0 = di_read_ticks();
            disc_b0 = di_bytes_read();
            dec0 = dec;
            decb0 = decb;
            card0 = exi_card_file_writes();
        }
        if (g_frames % 30 == 0) {  /* the overlay's FPS counter */
            static uint64_t t30;
            uint64_t now = rt_ticks();
            if (t30) overlay_set_fps((float)(30.0 * 40500000.0 / (double)(now - t30)));
            t30 = now;
        }
        /* performance line every 120 frames: presented frames and VI fields per second */
        static uint64_t t0;
        static uint32_t f0;
        static unsigned long long draws_acc, disc0;
        draws_acc += ndraws;
        if (g_frames % 120 == 0) {
            uint64_t t = rt_ticks();
            uint32_t f = vi_field_count();
            if (t0) {
                double secs = (double)(t - t0) / 40500000.0;
                rt_log("[perf] %.1f fps, %.1f retraces/s, %llu draws/frame, %llu MB disc, %llu AX frames, %d textures", 120 / secs,
                       (f - f0) / secs, draws_acc / 120, di_bytes_read() >> 20, ax_list_count(), gx_texture_live());
                /* where the guest thread's time went (ms per frame): textures decoded/hashed,
                 * blocked on the render thread, reading the disc image */
                double ms = 1000.0 / 40500000.0 / 120;
                uint64_t idle, late_avg, late_max;
                sched_stats(&idle, &late_avg, &late_max);
                rt_log("[perf]   draws/frame using indirect textures %llu (dynamic matrices not implemented), fog %llu (not implemented), Z textures %llu (not implemented)",
                       g_feat_ind / 120, g_feat_fog / 120, g_feat_ztex / 120);
                extern unsigned long long g_pipe_stores, g_pipe_bytes;
                static unsigned long long st0, by0;
                static uint64_t draw_t0;
                static unsigned long long verts0;
                rt_log("[perf]   GX pipe: %llu stores/frame (%llu KB); draw processing %.2f ms/frame (lookups included), "
                       "%llu vertices/frame", (g_pipe_stores - st0) / 120, (g_pipe_bytes - by0) / 120 >> 10,
                       (g_draw_total - draw_t0) * 1000.0 / 40500000.0 / 120, (g_verts_total - verts0) / 120);
                draw_t0 = g_draw_total;
                verts0 = g_verts_total;
                st0 = g_pipe_stores;
                by0 = g_pipe_bytes;
                rt_log("[perf]   EFB copies to textures/frame %.1f, from depth %.1f, target format codes seen %04X",
                       g_feat_copies / 120.0, g_feat_copy_z / 120.0, g_feat_copy_fmts);
                g_feat_ind = g_feat_fog = g_feat_ztex = g_feat_copies = g_feat_copy_z = 0;
                g_feat_copy_fmts = 0;
                rt_log("[perf]   ms/frame: textures %.2f, render wait %.2f, disc %.2f, guest idle %.2f; timer late avg %.3f max %.3f ms",
                       g_tex_ticks * ms, g_wait_ticks * ms, (di_read_ticks() - disc0) * ms, idle * ms, late_avg * ms * 120,
                       late_max * ms * 120);
                {
                    /* SR_PEEK=ADDR,LEN (hex): dump guest memory with each [perf] block (debugging) */
                    static int peek_init;
                    static uint32_t peek_addr, peek_len;
                    if (!peek_init++) {
                        const char* pk = getenv("SR_PEEK");
                        if (pk) sscanf(pk, "%x,%x", &peek_addr, &peek_len);
                    }
                    /* SR_POKE=ADDR,BYTE (hex): keep a guest byte set (debugging) */
                    static int poke_init;
                    static uint32_t poke_addr, poke_val;
                    if (!poke_init++) {
                        const char* pk = getenv("SR_POKE");
                        if (pk) sscanf(pk, "%x,%x", &poke_addr, &poke_val);
                    }
                    if (poke_addr) *mem_phys(poke_addr & 0x3FFFFFFF) = (uint8_t)poke_val;
                    for (uint32_t o = 0; o < peek_len; o += 32) {
                        char line[128];
                        int n = snprintf(line, sizeof line, "[peek] %08X:", peek_addr + o);
                        for (uint32_t k = o; k < o + 32 && k < peek_len; k++)
                            n += snprintf(line + n, sizeof line - n, "%s%02X", (k & 3) ? "" : " ", *mem_phys((peek_addr + k) & 0x3FFFFFFF));
                        rt_log("%s", line);
                    }
                }
                {
                    void dsp_aram_stats(uint64_t*, uint64_t*);
                    void di_late_stats(uint64_t*, uint64_t*, uint64_t*);
                    uint64_t ab, at, da, dm, dn;
                    dsp_aram_stats(&ab, &at);
                    di_late_stats(&da, &dm, &dn);
                    rt_log("[perf]   devices: ARAM DMA %llu KB in %.1f ms (game thread); %llu disc completions, late avg %.2f max %.2f ms",
                           (unsigned long long)(ab >> 10), at * ms * 120, (unsigned long long)dn, da * ms * 120, dm * ms * 120);
                }
            }
            t0 = t;
            f0 = f;
            draws_acc = 0;
            g_tex_ticks = g_wait_ticks = 0;
            disc0 = di_read_ticks();
        }
    }
}

void gx_bp_written(uint32_t reg, uint32_t value) {
    g_state_dirty = 1;
    if (reg == 0x52) efb_copy(value);
    else if (reg == 0x65) gx_texture_load_tlut();
}

unsigned long long gx_frames_presented(void) { return g_frames; }
