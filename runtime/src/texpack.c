/* Texture packs in Dolphin's format (custom textures).
 *
 * A mod can carry a `textures/` folder (any subfolders) of PNG files named the way Dolphin dumps and loads
 * them: tex1_<w>x<h>[_m]_<texture hash>[_<palette hash>|_$]_<format>.png. When the game uses a texture
 * whose name matches, the PNG is drawn instead, at its own resolution. So packs made for Dolphin work here
 * unchanged, and a pack made here works in Dolphin.
 *
 * The name follows Dolphin's TextureInfo::CalculateTextureName: XXH64 (seed 0) of the base level's bytes as
 * they are in memory (block-aligned size); for palette textures also XXH64 of the palette entries the texture
 * really uses (min..max index; "$" in a file name accepts any palette); "_m" when the texture is sampled with
 * mipmaps (TexMode0 mipmap filter on).
 *
 * DDS files (what most HD packs ship) are read too: BC1/BC2/BC3/BC7 (legacy FourCC or a DX10 header) and plain
 * 32-bit RGBA/BGRA; the base level is decoded to RGBA (bcdec) and the GPU makes the smaller levels.
 *
 * SR_DUMPTEX_DOLPHIN=<folder> (PC): writes every texture the game decodes as a PNG with that name, once each
 * (for making packs; also how the names were checked against Dolphin's own dumps). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "runtime.h"

#define STBI_ONLY_PNG

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x) ((void)0)
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "third_party/stb_image_write.h"
#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"

/* ---- XXH64 (the reference algorithm, seed 0 only as Dolphin uses it) ---- */
#define P1 0x9E3779B185EBCA87ull
#define P2 0xC2B2AE3D27D4EB4Full
#define P3 0x165667B19E3779F9ull
#define P4 0x85EBCA77C2B2AE63ull
#define P5 0x27D4EB2F165667C5ull
static inline uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static inline uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint64_t xround(uint64_t acc, uint64_t in) { acc += in * P2; acc = rotl64(acc, 31); return acc * P1; }
static inline uint64_t xmerge(uint64_t acc, uint64_t v) { acc ^= xround(0, v); return acc * P1 + P4; }
uint64_t texpack_xxh64(const uint8_t* p, size_t len) {
    const uint8_t* end = p + len;
    uint64_t h;
    if (len >= 32) {
        uint64_t v1 = P1 + P2, v2 = P2, v3 = 0, v4 = (uint64_t)0 - P1;
        const uint8_t* limit = end - 32;
        do {
            v1 = xround(v1, rd64(p)); v2 = xround(v2, rd64(p + 8));
            v3 = xround(v3, rd64(p + 16)); v4 = xround(v4, rd64(p + 24));
            p += 32;
        } while (p <= limit);
        h = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
        h = xmerge(h, v1); h = xmerge(h, v2); h = xmerge(h, v3); h = xmerge(h, v4);
    } else {
        h = P5;
    }
    h += (uint64_t)len;
    while (p + 8 <= end) { h ^= xround(0, rd64(p)); h = rotl64(h, 27) * P1 + P4; p += 8; }
    if (p + 4 <= end) { h ^= (uint64_t)rd32(p) * P1; h = rotl64(h, 23) * P2 + P3; p += 4; }
    while (p < end) { h ^= (uint64_t)*p * P5; h = rotl64(h, 11) * P1; p++; }
    h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
    return h;
}

/* ---- the index: file name (lower case, no extension) -> path ---- */
typedef struct { char* name; char* path; } Slot;
static Slot* g_slots;
static size_t g_cap, g_count;
static int g_inited, g_dump_inited;
static const char* g_dump_dir;
static unsigned g_replaced;

static uint64_t str_hash(const char* s) {
    uint64_t h = 1469598103934665603ull;
    while (*s) h = (h ^ (uint8_t)*s++) * 1099511628211ull;
    return h;
}
static Slot* find_slot(const char* name) {
    if (!g_cap) return NULL;
    for (size_t i = str_hash(name) % g_cap;; i = (i + 1) % g_cap) {
        if (!g_slots[i].name) return &g_slots[i];
        if (!strcmp(g_slots[i].name, name)) return &g_slots[i];
    }
}
static void add(const char* name, const char* path) {
    if (g_count * 2 >= g_cap) {
        size_t oc = g_cap;
        Slot* old = g_slots;
        g_cap = g_cap ? g_cap * 2 : 1024;
        g_slots = calloc(g_cap, sizeof *g_slots);
        if (!g_slots) rt_fatal("out of memory (texture packs)");
        for (size_t i = 0; i < oc; i++)
            if (old[i].name) *find_slot(old[i].name) = old[i];
        free(old);
    }
    Slot* s = find_slot(name);
    if (s->name) { free(s->path); s->path = strdup(path); return; }  /* a later mod wins */
    s->name = strdup(name);
    s->path = strdup(path);
    g_count++;
}

static void scan(const char* dir, int depth) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 8) scan(path, depth + 1);
            continue;
        }
        size_t n = strlen(e->d_name);
        if (n < 9 || strncmp(e->d_name, "tex1_", 5) != 0) continue;
        char name[256];
        if (n >= sizeof name) continue;
        for (size_t i = 0; i <= n; i++) name[i] = (char)(e->d_name[i] >= 'A' && e->d_name[i] <= 'Z' ? e->d_name[i] + 32 : e->d_name[i]);
        if (strcmp(name + n - 4, ".png") != 0 && strcmp(name + n - 4, ".dds") != 0) continue;
        name[n - 4] = 0;
        n -= 4;
        if (n > 4 && !strcmp(name + n - 4, "_arb")) name[n - 4] = 0;  /* Dolphin's "arbitrary mipmaps" mark */
        if (strstr(name, "_mip")) continue;  /* custom mip levels: the base level is used, the GPU makes the rest */
        add(name, path);
    }
    closedir(d);
}

int mods_count(void);
const char* mods_folder(int i);
const char* mods_base_dir(void);
int mods_enabled(int i);

static void init(void) {
    g_inited = 1;
    int packs = 0;
    for (int m = 0; m < mods_count(); m++) {
        if (!mods_enabled(m)) continue;
        char dir[512];
        snprintf(dir, sizeof dir, "%s/%s/textures", mods_base_dir(), mods_folder(m));
        size_t before = g_count;
        scan(dir, 0);
        if (g_count > before) {
            packs++;
            rt_log("[texpack] %s: %zu textures", mods_folder(m), g_count - before);
        }
    }
    if (packs) rt_log("[texpack] %zu custom textures from %d mod(s)", g_count, packs);
}

int texpack_active(void) {
    if (!g_inited) init();
    if (!g_dump_inited) {
        g_dump_inited = 1;
        g_dump_dir = getenv("SR_DUMPTEX_DOLPHIN");
        if (g_dump_dir) {
            mkdir(g_dump_dir
#ifndef _WIN32
                  , 0755
#endif
            );
            rt_log("[texpack] dumping textures with Dolphin's names to %s", g_dump_dir);
        }
    }
    return g_count > 0 || g_dump_dir != NULL;
}

/* Dolphin's name for a texture (see the top). src: the base level, size bytes (block-aligned);
 * tlut: the TMEM palette (NULL if not a palette format), tlut_bytes: 32 / 512 / 32768 */
void texpack_name(char* out, size_t n, const uint8_t* src, uint32_t size, int fmt, uint32_t w, uint32_t h, int mip,
                  const uint8_t* tlut, uint32_t tlut_bytes, int* has_tlut) {
    uint64_t th = 0;
    *has_tlut = 0;
    if (tlut && tlut_bytes) {
        uint32_t mn = 0xFFFF, mx = 0;
        if (tlut_bytes == 32) {
            for (uint32_t i = 0; i < size; i++) {
                uint32_t lo = src[i] & 15, hi = src[i] >> 4;
                if (lo < mn) mn = lo; if (hi < mn) mn = hi;
                if (lo > mx) mx = lo; if (hi > mx) mx = hi;
            }
        } else if (tlut_bytes == 512) {
            for (uint32_t i = 0; i < size; i++) { if (src[i] < mn) mn = src[i]; if (src[i] > mx) mx = src[i]; }
        } else {
            /* as Dolphin: it swaps a single byte (so this reads (byte << 8) & 0x3FFF) */
            for (uint32_t i = 0; i < size; i += 2) {
                uint32_t v = ((uint32_t)src[i] << 8) & 0x3FFF;
                if (v < mn) mn = v; if (v > mx) mx = v;
            }
        }
        if (mx >= mn) {
            th = texpack_xxh64(tlut + 2 * mn, 2 * (size_t)(mx + 1 - mn));
            *has_tlut = 1;
        }
    }
    uint64_t hsh = texpack_xxh64(src, size);
    if (*has_tlut)
        snprintf(out, n, "tex1_%ux%u%s_%016llx_%016llx_%d", w, h, mip ? "_m" : "", (unsigned long long)hsh,
                 (unsigned long long)th, fmt);
    else
        snprintf(out, n, "tex1_%ux%u%s_%016llx_%d", w, h, mip ? "_m" : "", (unsigned long long)hsh, fmt);
}

/* Largest side of a pack texture in memory: Switch keeps HD packs at <= 512 (a 4K pack decoded at full size would
 * not fit next to the game); PC uses the pack's own size. DDS: the matching smaller level is read from the file;
 * PNG: halved after loading. */
#ifdef __SWITCH__
#define TEXPACK_MAX 512
#else
#define TEXPACK_MAX 16384
#endif
static uint8_t* halve_rgba(uint8_t* px, int* w, int* h) {
    int nw = *w > 1 ? *w / 2 : 1, nh = *h > 1 ? *h / 2 : 1;
    uint8_t* o = malloc((size_t)nw * nh * 4);
    if (!o) { free(px); return NULL; }
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            int x0 = x * 2 < *w ? x * 2 : *w - 1, x1 = x * 2 + 1 < *w ? x * 2 + 1 : *w - 1;
            int y0 = y * 2 < *h ? y * 2 : *h - 1, y1 = y * 2 + 1 < *h ? y * 2 + 1 : *h - 1;
            for (int c = 0; c < 4; c++)
                o[((size_t)y * nw + x) * 4 + c] = (uint8_t)((px[((size_t)y0 * *w + x0) * 4 + c] + px[((size_t)y0 * *w + x1) * 4 + c] +
                                                              px[((size_t)y1 * *w + x0) * 4 + c] + px[((size_t)y1 * *w + x1) * 4 + c] + 2) / 4);
        }
    free(px);
    *w = nw;
    *h = nh;
    return o;
}

/* A DDS file's base level as RGBA8 (malloc'd), or NULL (logged). */
static uint8_t* load_dds(const char* path, int* ow, int* oh) {
    FILE* f = fopen(path, "rb");
    if (!f) { rt_log("[texpack] %s: can't open it", path); return NULL; }
    uint8_t hd[148];
    size_t got = fread(hd, 1, sizeof hd, f);
    const char* why = NULL;
    uint32_t h = 0, w = 0, off = 128, kind = 0, bgra = 0;  /* kind: 1 BC1, 2 BC2, 3 BC3, 7 BC7, 32 plain */
    if (got < 128 || memcmp(hd, "DDS ", 4) != 0) why = "not a DDS file";
    else {
        h = rd32(hd + 12); w = rd32(hd + 16);
        uint32_t pf_flags = rd32(hd + 80), bits = rd32(hd + 88);
        const uint8_t* cc = hd + 84;
        if (pf_flags & 4) {
            if (!memcmp(cc, "DXT1", 4)) kind = 1;
            else if (!memcmp(cc, "DXT2", 4) || !memcmp(cc, "DXT3", 4)) kind = 2;
            else if (!memcmp(cc, "DXT4", 4) || !memcmp(cc, "DXT5", 4)) kind = 3;
            else if (!memcmp(cc, "DX10", 4) && got >= 148) {
                uint32_t fmt = rd32(hd + 128);
                off = 148;
                if (fmt >= 70 && fmt <= 72) kind = 1;
                else if (fmt >= 73 && fmt <= 75) kind = 2;
                else if (fmt >= 76 && fmt <= 78) kind = 3;
                else if (fmt >= 97 && fmt <= 99) kind = 7;
                else if (fmt == 28 || fmt == 29) kind = 32;
                else if (fmt == 87 || fmt == 91) { kind = 32; bgra = 1; }
            }
        } else if ((pf_flags & 0x40) && bits == 32) {
            kind = 32;
            bgra = rd32(hd + 92) == 0x00FF0000u;  /* red mask in the third byte: BGRA */
        }
        if (!kind) why = "unsupported DDS format (BC1, BC2, BC3, BC7 or 32-bit only)";
        else if (!w || !h || w > 16384 || h > 16384) why = "bad size";
    }
    uint8_t* out = NULL;
    if (!why) {
        size_t bsz = kind == 1 ? 8 : 16;
        uint32_t mips = rd32(hd + 28), caps = rd32(hd + 8);
        if (!(caps & 0x20000) || mips < 1) mips = 1;  /* DDSD_MIPMAPCOUNT */
        while ((w > TEXPACK_MAX || h > TEXPACK_MAX) && mips > 1) {  /* skip to a smaller stored level */
            off += kind == 32 ? w * h * 4 : ((w + 3) / 4) * ((h + 3) / 4) * (uint32_t)bsz;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
            mips--;
        }
        uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        size_t need = kind == 32 ? (size_t)w * h * 4 : (size_t)bw * bh * bsz;
        uint8_t* data = malloc(need);
        out = malloc((size_t)bw * 4 * bh * 4 * 4);
        if (!data || !out) why = "out of memory";
        else if (fseek(f, (long)off, SEEK_SET) != 0 || fread(data, 1, need, f) != need) why = "file is cut short";
        else if (kind == 32) {
            memcpy(out, data, need);
            if (bgra)
                for (size_t i = 0; i < need; i += 4) { uint8_t t = out[i]; out[i] = out[i + 2]; out[i + 2] = t; }
        } else {
            int pitch = (int)(bw * 4 * 4);  /* decoded into a 4-aligned picture, cropped below */
            const uint8_t* b = data;
            for (uint32_t by = 0; by < bh; by++)
                for (uint32_t bx = 0; bx < bw; bx++, b += bsz) {
                    uint8_t* dst = out + ((size_t)by * 4 * pitch) + bx * 16;
                    if (kind == 1) bcdec_bc1(b, dst, pitch);
                    else if (kind == 2) bcdec_bc2(b, dst, pitch);
                    else if (kind == 3) bcdec_bc3(b, dst, pitch);
                    else bcdec_bc7(b, dst, pitch);
                }
            if (bw * 4 != w)
                for (uint32_t row = 1; row < h; row++) memmove(out + (size_t)row * w * 4, out + (size_t)row * pitch, (size_t)w * 4);
        }
        free(data);
    }
    fclose(f);
    if (why) {
        rt_log("[texpack] %s: can't read it (%s)", path, why);
        free(out);
        return NULL;
    }
    *ow = (int)w;
    *oh = (int)h;
    while (out && (*ow > TEXPACK_MAX || *oh > TEXPACK_MAX)) out = halve_rgba(out, ow, oh);
    return out;
}

/* the pack's picture for this name (RGBA8, malloc'd; free with free()), or NULL */
uint8_t* texpack_load(const char* name, int has_tlut, uint32_t* w, uint32_t* h) {
    Slot* s = find_slot(name);
    if ((!s || !s->name) && has_tlut) {  /* tex1_..._<hash>_$_<fmt>: any palette */
        char wild[256];
        const char* last = strrchr(name, '_');           /* _<fmt> */
        const char* pal = last ? last - 17 : NULL;        /* _<palette hash> */
        if (pal && pal > name && *pal == '_') {
            snprintf(wild, sizeof wild, "%.*s_$%s", (int)(pal - name), name, last);
            s = find_slot(wild);
        }
    }
    if ((!s || !s->name) && strlen(name) < 200) {  /* a pack made with the other "_m" (mipmaps on/off): same texture */
        char alt[256];
        const char* m = strstr(name, "_m_");
        if (m) snprintf(alt, sizeof alt, "%.*s%s", (int)(m - name), name, m + 2);
        else {
            const char* u = strchr(name + 5, '_');  /* after tex1_<w>x<h> */
            if (!u) return NULL;
            snprintf(alt, sizeof alt, "%.*s_m%s", (int)(u - name), name, u);
        }
        s = find_slot(alt);
    }
    if (!s || !s->name) return NULL;
    int x, y, c;
    size_t pl = strlen(s->path);
    uint8_t* px;
    if (pl > 4 && (s->path[pl - 1] | 32) == 's' && (s->path[pl - 2] | 32) == 'd') {
        px = load_dds(s->path, &x, &y);
        if (!px) return NULL;
    } else {
        px = stbi_load(s->path, &x, &y, &c, 4);
        if (!px) {
            rt_log("[texpack] %s: can't read it (%s)", s->path, stbi_failure_reason());
            return NULL;
        }
        while (px && (x > TEXPACK_MAX || y > TEXPACK_MAX)) px = halve_rgba(px, &x, &y);
        if (!px) return NULL;
    }
    *w = (uint32_t)x;
    *h = (uint32_t)y;
    if (g_replaced++ < 8 || (g_replaced & (g_replaced - 1)) == 0)
        rt_log("[texpack] %u textures replaced so far (latest %s, %dx%d)", g_replaced, name, x, y);
    return px;
}

void texpack_free(uint8_t* px) { stbi_image_free(px); }

/* SR_DUMPTEX_DOLPHIN: the decoded texture as <folder>/<name>.png, once per name */
void texpack_dump(const char* name, const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (!g_dump_dir) return;
    static Slot* seen;
    static size_t cap, cnt;
    if (cnt * 2 >= cap) {  /* a small set of names already written */
        size_t oc = cap;
        Slot* old = seen;
        cap = cap ? cap * 2 : 4096;
        seen = calloc(cap, sizeof *seen);
        for (size_t i = 0; i < oc; i++)
            if (old[i].name)
                for (size_t j = str_hash(old[i].name) % cap;; j = (j + 1) % cap)
                    if (!seen[j].name) { seen[j] = old[i]; break; }
        free(old);
    }
    for (size_t j = str_hash(name) % cap;; j = (j + 1) % cap) {
        if (!seen[j].name) { seen[j].name = strdup(name); cnt++; break; }
        if (!strcmp(seen[j].name, name)) return;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", g_dump_dir, name);
    stbi_write_png(path, (int)w, (int)h, 4, rgba, (int)w * 4);
}
