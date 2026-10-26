/* The fragment-shader specialization key: the BP registers (and the bits of them) that decide the
 * ubershader's control flow, read through CBP() in runtime/shaders/gx.frag. Register values that are
 * only data (TEV colors, konst, alpha refs, indirect matrices) are left out, so one specialized shader
 * serves many materials.
 *
 * tools/gen_shaders.py turns recorded keys into specialized shaders (CBP() returns these constants),
 * compiled ahead of time for deko3d. The PC build records the keys it meets (SR_SHADER_KEYS=<file>);
 * the Switch build logs the ones its table lacks to sdmc:/switch/sonicriders/shader_keys.txt.
 * Line format: <hash> <n> <reg>:<value> ... (hex). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "gx_shader_key.h"
#include "runtime.h"

static void add(GxFragKey* k, uint32_t reg, uint32_t val) {
    k->reg[k->n] = (uint8_t)reg;
    k->val[k->n] = val & 0xFFFFFF;
    k->n++;
}

void gx_frag_key(const uint32_t* bp, GxFragKey* k) {
    k->n = 0;
    uint32_t gen = bp[0x00] & (0xFu << 10 | 7u << 16);
    uint32_t stages = ((gen >> 10) & 15) + 1, nind = (gen >> 16) & 7;
    add(k, 0x00, gen);
    for (uint32_t i = 0; i < (stages + 1) / 2; i++) {  /* TEV orders: two stages per register */
        uint32_t v = bp[0x28 + i];
        if (2 * i + 1 >= stages) v &= 0xFFF;
        add(k, 0x28 + i, v);
    }
    for (uint32_t s = 0; s < stages; s++) {
        add(k, 0xC0 + 2 * s, bp[0xC0 + 2 * s]);
        add(k, 0xC1 + 2 * s, bp[0xC1 + 2 * s]);
    }
    /* swap tables (bits 0-3) and the konst selects of stages 2i, 2i+1: a stage's konst color select
     * counts only when one of its color inputs is KONST (14), the alpha select only when an alpha
     * input is (6). The game leaves selects set after a material that used them, so without this
     * every later state missed the specialized table (hardware log hw35). */
    uint32_t kc_used = 0, ka_used = 0;
    for (uint32_t s = 0; s < stages; s++) {
        uint32_t ce = bp[0xC0 + 2 * s], ae = bp[0xC1 + 2 * s];
        for (int f = 0; f < 16; f += 4)
            if (((ce >> f) & 15) == 14) kc_used |= 1u << s;
        for (int f = 4; f < 16; f += 3)
            if (((ae >> f) & 7) == 6) ka_used |= 1u << s;
    }
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t v = bp[0xF6 + i], m = 0xF;
        if (kc_used >> (2 * i) & 1) m |= 0x1F0;
        if (ka_used >> (2 * i) & 1) m |= 0x3E00;
        if (kc_used >> (2 * i + 1) & 1) m |= 0x7C000;
        if (ka_used >> (2 * i + 1) & 1) m |= 0xF80000;
        add(k, 0xF6 + i, v & m);
    }
    add(k, 0xF3, bp[0xF3] & 0xFF0000);  /* alpha test functions and logic */
    add(k, 0x42, bp[0x42] & 0x100);     /* constant ("destination") alpha enable */
    if (nind) {
        for (uint32_t s = 0; s < stages; s++) add(k, 0x10 + s, bp[0x10 + s]);
        /* scales and references of the indirect stages in use only (stage k: 0x25 + k / 2 bits
         * (k & 1) * 8 .. +7, 0x27 bits 6k .. 6k + 5) */
        add(k, 0x25, bp[0x25] & (nind > 1 ? 0xFFFFu : 0xFFu));
        add(k, 0x26, bp[0x26] & (nind > 3 ? 0xFFFFu : nind > 2 ? 0xFFu : 0u));
        add(k, 0x27, bp[0x27] & ((1u << (6 * (nind > 4 ? 4 : nind))) - 1));
    }
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < k->n; i++) {
        h = (h ^ k->reg[i]) * 0x100000001B3ull;
        h = (h ^ k->val[i]) * 0x100000001B3ull;
    }
    k->hash = h;
}

/* ---- recording keys (a set of hashes already written) ---- */
#define SEEN 16384
static uint64_t g_seen[SEEN];
static int g_nseen;
static FILE* g_out;
static int g_init;
static mtx_t g_lock;

void gx_frag_key_record(const GxFragKey* k, const char* path) {
    if (!g_init) {
        g_init = 1;
        mtx_init(&g_lock, mtx_plain);
        if (path) {
            /* keys already in the file count as seen */
            FILE* f = fopen(path, "r");
            if (f) {
                char line[2048];
                while (fgets(line, sizeof line, f) && g_nseen < SEEN) g_seen[g_nseen++] = strtoull(line, NULL, 16);
                fclose(f);
            }
            g_out = fopen(path, "a");
        }
        if (g_out) rt_log("[shader] recording fragment-shader keys to %s (%d known)", path, g_nseen);
    }
    if (!g_out) return;
    mtx_lock(&g_lock);
    for (int i = 0; i < g_nseen; i++)
        if (g_seen[i] == k->hash) { mtx_unlock(&g_lock); return; }
    if (g_nseen < SEEN) g_seen[g_nseen++] = k->hash;
    fprintf(g_out, "%016llx %d", (unsigned long long)k->hash, k->n);
    for (int i = 0; i < k->n; i++) fprintf(g_out, " %02x:%06x", k->reg[i], k->val[i]);
    fprintf(g_out, "\n");
    fflush(g_out);
    mtx_unlock(&g_lock);
}
