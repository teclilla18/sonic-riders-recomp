/* Read files straight from the user's GameCube disc image (GCM/ISO, 1,459,978,240 bytes). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

static FILE* g_disc;
static uint8_t g_header[0x440];
static uint8_t* g_fst;
static uint32_t g_fst_size;

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static int name_eq(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

size_t disc_read(uint64_t offset, void* dst, size_t n) {
#ifdef _WIN32
    _fseeki64(g_disc, (long long)offset, SEEK_SET);
#else
    fseeko(g_disc, (off_t)offset, SEEK_SET);
#endif
    return fread(dst, 1, n, g_disc);
}

int disc_open(const char* path) {
    g_disc = fopen(path, "rb");
    if (!g_disc) return 0;
    if (disc_read(0, g_header, sizeof g_header) != sizeof g_header) return 0;
    if (be32(g_header + 0x1C) != 0xC2339F3Du) rt_fatal("%s is not a GameCube disc image", path);
    if (memcmp(g_header, "GXEE8P", 6) != 0 || g_header[7] != 0)
        rt_fatal("%s is %.6s rev %d; this port is built for Sonic Riders USA (GXEE8P) rev 0", path,
                 (const char*)g_header, g_header[7]);
    uint32_t fst_off = be32(g_header + 0x424);
    g_fst_size = be32(g_header + 0x428);
    g_fst = malloc(g_fst_size);
    disc_read(fst_off, g_fst, g_fst_size);
    rt_log("[disc] %s: %.6s \"%s\", FST %u bytes", path, (const char*)g_header, (const char*)g_header + 0x20, g_fst_size);
    return 1;
}

const uint8_t* disc_header(void) { return g_header; }
const uint8_t* disc_fst(uint32_t* size) { *size = g_fst_size; return g_fst; }

/* the file holding disc offset `off` (its name and the offset into it), for logs */
const char* disc_name_at(uint64_t off, uint32_t* rel) {
    uint32_t n = be32(g_fst + 8);
    const char* names = (const char*)g_fst + n * 12;
    for (uint32_t i = 1; i < n; i++) {
        const uint8_t* e = g_fst + i * 12;
        if (e[0]) continue;
        uint32_t fo = be32(e + 4), fs = be32(e + 8);
        if (off >= fo && off < (uint64_t)fo + fs) {
            *rel = (uint32_t)(off - fo);
            return names + (be32(e) & 0xFFFFFF);
        }
    }
    *rel = 0;
    return "?";
}

/* path like "/_Main.rel" or "dir/file" (case-insensitive, as DVDConvertPathToEntrynum) */
int disc_find(const char* path, uint32_t* offset, uint32_t* size) {
    uint32_t n = be32(g_fst + 8);
    const char* names = (const char*)g_fst + n * 12;
    uint32_t dir = 0, i = 1;
    while (*path == '/') path++;
    while (*path) {
        const char* end = strchr(path, '/');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        uint32_t stop = dir == 0 ? n : be32(g_fst + dir * 12 + 8);
        int found = 0;
        while (i < stop) {
            const uint8_t* e = g_fst + i * 12;
            const char* nm = names + (be32(e) & 0xFFFFFF);
            if (strlen(nm) == len && name_eq(nm, path, len)) { found = 1; break; }
            i = (e[0] ? be32(e + 8) : i + 1);
        }
        if (!found) return 0;
        const uint8_t* e = g_fst + i * 12;
        if (!end) {
            if (e[0]) return 0;
            *offset = be32(e + 4);
            *size = be32(e + 8);
            return 1;
        }
        if (!e[0]) return 0;
        dir = i++;
        path = end + 1;
    }
    return 0;
}
