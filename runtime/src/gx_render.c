/* Guest side of the renderer: decodes draws and EFB copies from the GX command stream into the
 * current frame's command list (gx_render.h). Runs on the guest thread that writes the FIFO. */
#include <stdlib.h>
#include <string.h>

#include "gx.h"
#include "gx_render.h"
#include "runtime.h"

void gx_texture_load_tlut(void);
int gx_texture_lookup(int map);
int gx_texture_copy_target(uint32_t addr, uint32_t w, uint32_t h);
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

static GxFrame* frame(void) {
    if (!g_frame) {
        g_frame = render_take_free_frame();
        g_frame->ncmds = g_frame->nverts = g_frame->nunis = g_frame->nretired = 0;
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

static void push_state(void) {
    GxFrame* f = frame();
    f->unis = grow(f->unis, &f->cap_unis, f->nunis + 1, sizeof(GxUniforms));
    GxUniforms* u = &f->unis[f->nunis];
    memcpy(u->bp, g_gx.bp, sizeof u->bp);
    memcpy(u->xfr, g_gx.xf_reg, sizeof u->xfr);
    memcpy(u->xfm, g_gx.xf_mem, sizeof u->xfm);
    memcpy(u->konst, g_gx.konst, sizeof u->konst);
    GxCmd* c = push_cmd(GXC_STATE);
    c->a = f->nunis++;
    g_state_dirty = 0;
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

/* pointer to attribute data: direct (in the stream) or indexed into the array `arr` */
static const uint8_t* attr(const uint8_t** pp, int type, int arr) {
    if (type == 1) return *pp;
    uint32_t idx = type == 2 ? (*pp)[0] : rd16(*pp);
    *pp += type == 2 ? 1 : 2;
    uint32_t base = g_gx.cp[0xA0 + arr] & 0x03FFFFFF, stride = g_gx.cp[0xB0 + arr] & 0xFF;
    return mem_phys(base + idx * stride);
}

static void decode_vertex(const uint8_t** pp, int vat, GxVertex* v) {
    uint32_t lo = g_gx.cp[0x50], hi = g_gx.cp[0x60];
    uint32_t a = g_gx.cp[0x70 + vat], b = g_gx.cp[0x80 + vat], cc = g_gx.cp[0x90 + vat];
    uint32_t mia = g_gx.cp[0x30], mib = g_gx.cp[0x40];
    const uint8_t* p = *pp;
    memset(v, 0, sizeof *v);
    v->pos[3] = (float)(mia & 63);
    for (int i = 0; i < 8; i++) v->tmtx[i] = (float)(i < 4 ? (mia >> (6 + 6 * i)) & 63 : (mib >> (6 * (i - 4))) & 63);
    if (lo & 1) v->pos[3] = (float)*p++;
    for (int i = 0; i < 8; i++)
        if ((lo >> (1 + i)) & 1) v->tmtx[i] = (float)*p++;
    /* position */
    int t = (lo >> 9) & 3;
    if (t) {
        const uint8_t* q = attr(&p, t, 0);
        int fmt = (a >> 1) & 7, n = (a & 1) ? 3 : 2;
        float s = 1.0f / (float)(1u << ((a >> 4) & 31));
        for (int k = 0; k < n; k++) v->pos[k] = comp(&q, fmt, s);
        if (t == 1) p += n * k_csize[fmt];
    }
    /* normal (the first of NBT only) */
    t = (lo >> 11) & 3;
    if (t) {
        int fmt = (a >> 10) & 7, nbt = (a >> 9) & 1, three_idx = nbt && ((a >> 31) & 1);
        float s = fmt == 1 ? 1.0f / 64 : fmt == 3 ? 1.0f / 16384 : 1.0f;
        const uint8_t* q = attr(&p, t, 1);
        for (int k = 0; k < 3; k++) v->nrm[k] = comp(&q, fmt, s);
        if (t == 1) p += (nbt ? 9 : 3) * k_csize[fmt];
        else if (three_idx) p += 2 * (t == 2 ? 1 : 2);
    }
    /* colors */
    for (int c = 0; c < 2; c++) {
        t = (lo >> (13 + 2 * c)) & 3;
        int fmt = c == 0 ? (a >> 14) & 7 : (a >> 18) & 7;
        if (!t) { v->col[c][0] = v->col[c][1] = v->col[c][2] = v->col[c][3] = 1.0f; continue; }
        const uint8_t* q = attr(&p, t, 2 + c);
        color(q, fmt, v->col[c]);
        if (t == 1) p += k_colsize[fmt];
    }
    /* texture coordinates */
    static const struct { int word, cnt, fmt, shft; } tex[8] = {
        {0, 21, 22, 25}, {1, 0, 1, 4}, {1, 9, 10, 13}, {1, 18, 19, 22}, {1, 27, 28, -1}, {2, 5, 6, 9}, {2, 14, 15, 18}, {2, 23, 24, 27}};
    for (int i = 0; i < 8; i++) {
        t = (hi >> (2 * i)) & 3;
        if (!t) continue;
        uint32_t w = tex[i].word == 0 ? a : tex[i].word == 1 ? b : cc;
        int fmt = (w >> tex[i].fmt) & 7, n = ((w >> tex[i].cnt) & 1) ? 2 : 1;
        int shft = tex[i].shft >= 0 ? (int)((w >> tex[i].shft) & 31) : (int)(cc & 31);  /* TEX4SHFT is in VAT C */
        float s = 1.0f / (float)(1u << shft);
        const uint8_t* q = attr(&p, t, 4 + i);
        for (int k = 0; k < n; k++) v->tex[i][k] = comp(&q, fmt, s);
        if (t == 1) p += n * k_csize[fmt];
    }
    *pp = p;
}

void gx_draw(int prim, int vat, uint32_t count, const uint8_t* vertices, uint32_t vertex_size) {
    (void)vertex_size;
    if (!count) return;
    if (g_state_dirty) push_state();
    GxFrame* f = frame();
    /* decode into a scratch list, then expand to triangle/line/point lists */
    static GxVertex* tmp;
    static uint32_t tmp_cap;
    tmp = grow(tmp, &tmp_cap, count, sizeof(GxVertex));
    const uint8_t* p = vertices;
    for (uint32_t i = 0; i < count; i++) decode_vertex(&p, vat, &tmp[i]);
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
    f->verts = grow(f->verts, &f->cap_verts, f->nverts + out_n, sizeof(GxVertex));
    GxVertex* o = f->verts + f->nverts;
    uint32_t k = 0;
    switch (prim) {
    case 0: case 1:
        for (uint32_t i = 0; i + 3 < count; i += 4) {
            o[k++] = tmp[i]; o[k++] = tmp[i + 1]; o[k++] = tmp[i + 2];
            o[k++] = tmp[i]; o[k++] = tmp[i + 2]; o[k++] = tmp[i + 3];
        }
        break;
    case 3:
        for (uint32_t i = 2; i < count; i++) {
            if (i & 1) { o[k++] = tmp[i - 1]; o[k++] = tmp[i - 2]; o[k++] = tmp[i]; }
            else { o[k++] = tmp[i - 2]; o[k++] = tmp[i - 1]; o[k++] = tmp[i]; }
        }
        break;
    case 4:
        for (uint32_t i = 2; i < count; i++) { o[k++] = tmp[0]; o[k++] = tmp[i - 1]; o[k++] = tmp[i]; }
        break;
    case 6:
        for (uint32_t i = 1; i < count; i++) { o[k++] = tmp[i - 1]; o[k++] = tmp[i]; }
        break;
    default:
        memcpy(o, tmp, out_n * sizeof(GxVertex));
        k = out_n;
        break;
    }
    GxCmd* c = push_cmd(GXC_DRAW);
    c->prim = (uint8_t)kind;
    c->a = f->nverts;
    c->b = k;
    /* textures used by the enabled TEV stages */
    for (int i = 0; i < 8; i++) c->tex[i] = -1;
    int stages = (int)((g_gx.bp[0x00] >> 10) & 15) + 1;
    for (int s = 0; s < stages; s++) {
        uint32_t order = g_gx.bp[0x28 + s / 2] >> ((s & 1) ? 12 : 0);
        int map = (int)(order & 7);
        if ((order >> 6) & 1 && c->tex[map] < 0) c->tex[map] = gx_texture_lookup(map);
    }
    f->nverts += k;
}

/* BP 0x52: copy the EFB (or a part) to an XFB or a texture, optionally clearing it */
static void efb_copy(uint32_t v) {
    uint32_t tl = g_gx.bp[0x49], wh = g_gx.bp[0x4A];
    uint32_t x = tl & 0x3FF, y = (tl >> 10) & 0x3FF, w = (wh & 0x3FF) + 1, h = ((wh >> 10) & 0x3FF) + 1;
    uint32_t dest = (g_gx.bp[0x4B] & 0xFFFFFF) << 5;
    int to_xfb = (v >> 14) & 1, clear = (v >> 11) & 1, half = (v >> 9) & 1;
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
        push_cmd(GXC_PRESENT);
        uint32_t ndraws = 0;
        for (uint32_t i = 0; i < g_frame->ncmds; i++) ndraws += g_frame->cmds[i].type == GXC_DRAW;
        render_submit(g_frame);
        g_frame = NULL;
        g_frames++;
        /* performance line every 120 frames: presented frames and VI fields per second */
        static uint64_t t0;
        static uint32_t f0;
        static unsigned long long draws_acc;
        draws_acc += ndraws;
        if (g_frames % 120 == 0) {
            uint64_t t = rt_ticks();
            uint32_t f = vi_field_count();
            if (t0) {
                double secs = (double)(t - t0) / 40500000.0;
                rt_log("[perf] %.1f fps, %.1f retraces/s, %llu draws/frame, %llu MB disc, %llu AX frames", 120 / secs,
                       (f - f0) / secs, draws_acc / 120, di_bytes_read() >> 20, ax_list_count());
            }
            t0 = t;
            f0 = f;
            draws_acc = 0;
        }
    }
}

void gx_bp_written(uint32_t reg, uint32_t value) {
    g_state_dirty = 1;
    if (reg == 0x52) efb_copy(value);
    else if (reg == 0x65) gx_texture_load_tlut();
}

unsigned long long gx_frames_presented(void) { return g_frames; }
