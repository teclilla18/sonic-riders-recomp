/* GX textures: decoding the tiled big-endian formats to RGBA8, TLUTs in a TMEM model, and a cache
 * keyed by (address, format, size, palette) plus a hash of the bytes, so textures the game rewrites
 * are decoded again. EFB copies made to an address are found here too (gx_texture_set_copy).
 * Formats: YAGCD "Texture formats", Dolphin's TextureDecoder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "gx.h"
#include "gx_render.h"
#include "runtime.h"

static uint8_t g_tmem[0x100000];  /* 1 MiB: TLUTs are loaded into the upper half */

#define MAX_TEX 65536
static GxTexture* g_tex[MAX_TEX];
static int g_ntex;
/* ids freed by the backend (render thread), reused by new_texture (GX thread) */
static int g_free_ids[MAX_TEX];
static int g_nfree_ids;
static mtx_t g_ids_lock;
static once_flag g_ids_once = ONCE_FLAG_INIT;
static void ids_init(void) { mtx_init(&g_ids_lock, mtx_plain); }

typedef struct Entry { uint64_t key, hash, full; int id, valid; uint32_t frame; } Entry;
#define CACHE_SIZE 8192
static Entry g_cache[CACHE_SIZE];

/* EFB copy targets by destination address */
#define MAX_COPIES 256
static struct { uint32_t addr, w, h; int id; } g_copies[MAX_COPIES];
static int g_ncopies;

static uint32_t g_frame;

GxTexture* gx_texture_get(int id) { return id >= 0 && id < g_ntex ? g_tex[id] : NULL; }

/* backend: the frame that retired `id` is done */
void gx_texture_free(int id) {
    if (id < 0 || id >= g_ntex || !g_tex[id]) return;
    free(g_tex[id]->rgba);
    free(g_tex[id]);
    g_tex[id] = NULL;
    call_once(&g_ids_once, ids_init);
    mtx_lock(&g_ids_lock);
    g_free_ids[g_nfree_ids++] = id;
    mtx_unlock(&g_ids_lock);
}

/* textures alive (allocated and not yet freed by the backend), for the [perf] line */
int gx_texture_live(void) {
    call_once(&g_ids_once, ids_init);
    mtx_lock(&g_ids_lock);
    int n = g_ntex - g_nfree_ids;
    mtx_unlock(&g_ids_lock);
    return n;
}

/* textures decoded so far (count, RGBA bytes), for the [hitch] lines */
static unsigned long long g_decoded, g_decoded_bytes;
void gx_texture_decode_stats(unsigned long long* count, unsigned long long* bytes) {
    *count = g_decoded;
    *bytes = g_decoded_bytes;
}

static GxTexture* new_texture(uint32_t w, uint32_t h, int is_copy) {
    call_once(&g_ids_once, ids_init);
    mtx_lock(&g_ids_lock);
    int id = g_nfree_ids ? g_free_ids[--g_nfree_ids] : -1;
    mtx_unlock(&g_ids_lock);
    if (id < 0) {
        if (g_ntex >= MAX_TEX) rt_fatal("too many textures");
        id = g_ntex++;
    }
    GxTexture* t = calloc(1, sizeof *t);
    t->id = id;
    t->width = w;
    t->height = h;
    t->is_copy = is_copy;
    if (!is_copy) t->rgba = malloc((size_t)w * h * 4);
    g_tex[id] = t;
    return t;
}

static uint64_t hash_bytes(const uint8_t* p, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 29;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* a cheap per-frame check: 512 8-byte samples spread over the texture (all of it when small) */
static uint64_t hash_sampled(const uint8_t* p, size_t n) {
    if (n <= 4096) return hash_bytes(p, n);
    uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
    size_t step = (n - 8) / 511;
    for (int i = 0; i < 512; i++) {
        uint64_t v;
        memcpy(&v, p + (size_t)i * step, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 29;
    }
    return h;
}

static const int k_bw[16] = {8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0};
static const int k_bh[16] = {8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0};

uint32_t gx_texture_size(int fmt, uint32_t w, uint32_t h) {
    if (fmt > 15 || !k_bw[fmt]) return 0;
    uint32_t bx = (w + k_bw[fmt] - 1) / k_bw[fmt], by = (h + k_bh[fmt] - 1) / k_bh[fmt];
    return bx * by * (fmt == 6 ? 64 : 32);
}

static inline void put(uint8_t* o, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b; o[3] = (uint8_t)a;
}
static inline void from565(uint8_t* o, uint16_t v) {
    uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    put(o, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
}
static inline void from5a3(uint8_t* o, uint16_t v) {
    if (v & 0x8000) {
        uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        put(o, (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255);
    } else {
        uint32_t a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        put(o, r * 17, g * 17, b * 17, (a << 5) | (a << 2) | (a >> 1));
    }
}
static inline void from_tlut(uint8_t* o, uint16_t v, int tfmt) {
    if (tfmt == 0) put(o, v & 0xFF, v & 0xFF, v & 0xFF, v >> 8);   /* IA8: alpha high, intensity low */
    else if (tfmt == 1) from565(o, v);
    else from5a3(o, v);
}
static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

void gx_frame_retire(int id);

/* SR_DUMPTEX: every decoded texture to build/tex/<id>_f<format>_t<tlut format>_<address>_<w>x<h>.tga
 * (debugging decode problems, desktop) */
static void dump_texture(const GxTexture* t, int fmt, int tfmt, uint32_t addr) {
    static int on = -1;
    if (on < 0) on = getenv("SR_DUMPTEX") != NULL;
    if (!on) return;
    char path[128];
    snprintf(path, sizeof path, "build/tex/%04d_f%d_t%d_%08X_%ux%u.tga", t->id, fmt, tfmt, addr, t->width, t->height);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint8_t hdr[18] = {0, 0, 2};
    hdr[12] = (uint8_t)t->width; hdr[13] = (uint8_t)(t->width >> 8);
    hdr[14] = (uint8_t)t->height; hdr[15] = (uint8_t)(t->height >> 8);
    hdr[16] = 32; hdr[17] = 0x28;  /* 32 bpp, top-left origin, 8 alpha bits */
    fwrite(hdr, 1, 18, f);
    for (uint32_t i = 0; i < t->width * t->height; i++) {
        const uint8_t* p = t->rgba + 4 * i;
        uint8_t bgra[4] = {p[2], p[1], p[0], p[3]};
        fwrite(bgra, 1, 4, f);
    }
    fclose(f);
}

static void decode(uint8_t* out, const uint8_t* src, int fmt, uint32_t w, uint32_t h, const uint8_t* tlut, int tfmt) {
    int bw = k_bw[fmt], bh = k_bh[fmt];
    uint32_t bx_n = (w + bw - 1) / bw, by_n = (h + bh - 1) / bh;
    const uint8_t* s = src;
    for (uint32_t by = 0; by < by_n; by++)
        for (uint32_t bx = 0; bx < bx_n; bx++) {
            if (fmt == 6) {  /* RGBA8: 32 bytes of AR pairs, then 32 bytes of GB pairs */
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++) {
                        uint32_t px = bx * 4 + x, py = by * 4 + y;
                        int i = y * 4 + x;
                        if (px < w && py < h)
                            put(out + (py * w + px) * 4, s[2 * i + 1], s[32 + 2 * i], s[32 + 2 * i + 1], s[2 * i]);
                    }
                s += 64;
                continue;
            }
            if (fmt == 14) {  /* CMPR: four DXT1 sub-blocks, big-endian */
                for (int sb = 0; sb < 4; sb++, s += 8) {
                    uint16_t c0 = be16(s), c1 = be16(s + 2);
                    uint8_t pal[4][4];
                    from565(pal[0], c0);
                    from565(pal[1], c1);
                    if (c0 > c1) {
                        for (int k = 0; k < 3; k++) {
                            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
                            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
                        }
                        pal[2][3] = pal[3][3] = 255;
                    } else {
                        for (int k = 0; k < 3; k++) pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
                        pal[2][3] = 255;
                        memset(pal[3], 0, 4);
                    }
                    for (int y = 0; y < 4; y++) {
                        uint8_t bits = s[4 + y];
                        for (int x = 0; x < 4; x++) {
                            uint32_t px = bx * 8 + (sb & 1) * 4 + x, py = by * 8 + (sb >> 1) * 4 + y;
                            if (px < w && py < h) memcpy(out + (py * w + px) * 4, pal[(bits >> (6 - 2 * x)) & 3], 4);
                        }
                    }
                }
                continue;
            }
            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++) {
                    uint32_t px = bx * bw + x, py = by * bh + y;
                    uint8_t* o = out + (py * w + px) * 4;
                    int i = y * bw + x;
                    uint8_t tmp[4];
                    if (px >= w || py >= h) o = tmp;
                    switch (fmt) {
                    case 0: { uint32_t v = (s[i >> 1] >> ((i & 1) ? 0 : 4)) & 15; v *= 17; put(o, v, v, v, v); break; }
                    case 1: put(o, s[i], s[i], s[i], s[i]); break;
                    case 2: { uint32_t a = (s[i] >> 4) * 17, v = (s[i] & 15) * 17; put(o, v, v, v, a); break; }
                    case 3: put(o, s[2 * i + 1], s[2 * i + 1], s[2 * i + 1], s[2 * i]); break;
                    case 4: from565(o, be16(s + 2 * i)); break;
                    case 5: from5a3(o, be16(s + 2 * i)); break;
                    case 8: { uint32_t idx = (s[i >> 1] >> ((i & 1) ? 0 : 4)) & 15; from_tlut(o, be16(tlut + 2 * idx), tfmt); break; }
                    case 9: from_tlut(o, be16(tlut + 2 * s[i]), tfmt); break;
                    case 10: { uint32_t idx = be16(s + 2 * i) & 0x3FFF; from_tlut(o, be16(tlut + 2 * idx), tfmt); break; }
                    default: put(o, 255, 0, 255, 255); break;
                    }
                }
            s += 32;
        }
}

/* BP 0x64/0x65: copy a TLUT from RAM into TMEM */
void gx_texture_load_tlut(void) {
    uint32_t src = (g_gx.bp[0x64] & 0xFFFFFF) << 5;
    uint32_t tmem = (g_gx.bp[0x65] & 0x3FF) << 9, count = ((g_gx.bp[0x65] >> 10) & 0x7FF) * 32;
    if (tmem + count > sizeof g_tmem) count = (uint32_t)sizeof g_tmem - tmem;
    if (src + count <= GUEST_RAM_SIZE) memcpy(g_tmem + tmem, mem_phys(src), count);
}

void gx_texture_set_copy(uint32_t addr, uint32_t w, uint32_t h, int id) {
    for (int i = 0; i < g_ncopies; i++)
        if (g_copies[i].addr == addr) {
            if (g_copies[i].id != id) gx_frame_retire(g_copies[i].id);  /* replaced by a copy of another size */
            g_copies[i].w = w; g_copies[i].h = h; g_copies[i].id = id;
            return;
        }
    if (g_ncopies < MAX_COPIES) {
        g_copies[g_ncopies].addr = addr; g_copies[g_ncopies].w = w; g_copies[g_ncopies].h = h;
        g_copies[g_ncopies].id = id; g_ncopies++;
    }
}

/* the EFB copy target texture for (addr, w, h), creating it if needed */
int gx_texture_copy_target(uint32_t addr, uint32_t w, uint32_t h) {
    for (int i = 0; i < g_ncopies; i++)
        if (g_copies[i].addr == addr && g_copies[i].w == w && g_copies[i].h == h) return g_copies[i].id;
    GxTexture* t = new_texture(w, h, 1);
    gx_texture_set_copy(addr, w, h, t->id);
    return t->id;
}

/* Cached textures the game has not drawn with for a while are released, so the backend's texture
 * memory holds roughly the working set (the Switch's image pool is fixed). */
#define EVICT_AFTER 600  /* frames */
void gx_texture_new_frame(void) {
    g_frame++;
    if (g_frame % 64) return;
    for (int i = 0; i < CACHE_SIZE; i++) {
        Entry* e = &g_cache[i];
        if (e->valid && g_frame - e->frame > EVICT_AFTER) {
            gx_frame_retire(e->id);
            e->valid = 0;
        }
    }
}

/* the texture bound to texmap `map` by the current BP state; -1 if none */
int gx_texture_lookup(int map) {
    int hi = map >= 4, i = map & 3;
    uint32_t img0 = g_gx.bp[(hi ? 0xA8 : 0x88) + i], img3 = g_gx.bp[(hi ? 0xB4 : 0x94) + i];
    uint32_t tlutr = g_gx.bp[(hi ? 0xB8 : 0x98) + i];
    uint32_t w = (img0 & 0x3FF) + 1, h = ((img0 >> 10) & 0x3FF) + 1, fmt = (img0 >> 20) & 15;
    uint32_t addr = (img3 & 0xFFFFFF) << 5;
    for (int k = 0; k < g_ncopies; k++)  /* render-to-texture result at this address */
        if (g_copies[k].addr == addr && g_copies[k].w == w && g_copies[k].h == h) return g_copies[k].id;
    uint32_t size = gx_texture_size((int)fmt, w, h);
    if (!size || addr + size > GUEST_RAM_SIZE) {
        static int logged;
        if (logged++ < 20) rt_log("[tex] map %d: no texture (fmt %u %ux%u at %08X)", map, fmt, w, h, addr);
        return -1;
    }
    const uint8_t* src = mem_phys(addr);
    const uint8_t* tlut = NULL;
    int tfmt = 0;
    uint32_t tlut_bytes = 0;
    if (fmt == 8 || fmt == 9 || fmt == 10) {
        uint32_t toff = (tlutr & 0x3FF) << 9;
        tfmt = (tlutr >> 10) & 3;
        tlut_bytes = fmt == 8 ? 32 : fmt == 9 ? 512 : 32768;
        if (toff + tlut_bytes > sizeof g_tmem) return -1;
        tlut = g_tmem + toff;
    }
    uint64_t key = (uint64_t)addr << 32 ^ (uint64_t)fmt << 28 ^ (uint64_t)w << 14 ^ h ^ ((uint64_t)tfmt << 62);
    /* two-way set: slots i and i ^ 1 of a well-mixed index; a miss replaces the free or older one */
    uint64_t m = key * 0x9E3779B97F4A7C15ull;
    m = (m ^ (m >> 31)) * 0xBF58476D1CE4E5B9ull;
    m ^= m >> 29;
    Entry* e = &g_cache[m % CACHE_SIZE];
    Entry* e2 = &g_cache[(m % CACHE_SIZE) ^ 1];
    if (!(e->valid && e->key == key) &&
        ((e2->valid && e2->key == key) || (e->valid && (!e2->valid || e2->frame < e->frame))))
        e = e2;
    if (e->valid && e->key == key && e->frame == g_frame) return e->id;  /* already checked this frame */
    /* changed? a sampled hash every frame, the full hash every 16th frame (staggered), the palette
     * always: textures the game rewrites in part are noticed within 16 frames */
    uint64_t th = tlut ? hash_bytes(tlut, tlut_bytes) * 31 : 0;
    uint64_t hsh = hash_sampled(src, size) ^ th;
    if (e->valid && e->key == key && e->hash == hsh) {
        if (((g_frame + (uint32_t)(key >> 5)) & 15) || (hash_bytes(src, size) ^ th) == e->full) {
            e->frame = g_frame;
            return e->id;
        }
    }
    /* contents changed: a new texture (the render thread may still be uploading the old one from the
     * frame in flight); the old one is retired with this frame and freed by the backend after it */
    if (e->valid) gx_frame_retire(e->id);  /* changed, or another texture in this slot */
    GxTexture* t = new_texture(w, h, 0);
    g_decoded++;
    g_decoded_bytes += (unsigned long long)w * h * 4;
    decode(t->rgba, src, (int)fmt, w, h, tlut, tfmt);
    t->gen++;
    dump_texture(t, (int)fmt, tfmt, addr);
    e->key = key;
    e->hash = hsh;
    e->full = hash_bytes(src, size) ^ th;
    e->id = t->id;
    e->valid = 1;
    e->frame = g_frame;
    return t->id;
}
