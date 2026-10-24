/* SR_DFF=<frame>[,<count>] (debugging, desktop): record presented frames (the last `count`, default
 * 2, up to and including <frame>) as a Dolphin FIFO log (.dff), so Dolphin's FIFO player draws
 * exactly the frames we drew: a renderer comparison with nothing else different (the game's demos
 * do not replay identically between the two). Written to build/frame_<frame>.dff. Two frames or more
 * let the later ones find the earlier ones' EFB copies (the speed-blur overlays) in Dolphin.
 *
 * What is recorded (GP thread): at the end of the previous frame (its XFB copy) the GP registers (BP,
 * CP, XF memory and registers) and TMEM; then every command of the immediate stream until this
 * frame's XFB copy; the display lists, indexed XF loads and TLUT loads as they were when the GP read
 * them; at the end, all of main RAM as the first memory update (vertex arrays and textures come from
 * it: record with SR_GX_SYNC=1 so it matches what was drawn). Textures made by earlier frames' EFB
 * copies are not in RAM here (ours live on the GPU), so those draws differ.
 *
 * Layout: Dolphin Source/Core/Core/FifoPlayer/FifoDataFile.cpp (FileHeader 128 bytes, FileFrameInfo
 * 64, FileMemoryUpdate 24; register arrays as little-endian u32; version 5, loader 1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gx.h"
#include "runtime.h"

const uint8_t* gx_tmem(void);

static int g_target = -2;  /* -2: not read yet; -1: off */
static int g_state;        /* 0 waiting, 1 recording, 2 done, 3 recording the last command */
static int g_skip;         /* the command in progress (the previous frame's XFB copy) is not recorded */
int g_dff_active;          /* gx_fifo.c calls gx_dff_cmd only while this is set */
static uint8_t* g_buf;
static size_t g_len, g_cap;
static uint32_t g_bp[256], g_cp[256], g_xfm[4096], g_xfr[88];
static uint8_t* g_tmem_copy;
/* memory the GP read during the frame, as it was then (display lists, indexed XF loads, TLUT loads):
 * the game reuses buffers, so the end-of-frame RAM alone replays other objects' data */
typedef struct { uint32_t pos, addr, size, off, frame; } Upd;
#define MAX_FRAMES 8
static int g_count = 2, g_cur;                 /* frames to record; the one being recorded */
static size_t g_fstart[MAX_FRAMES + 1];        /* each frame's start in g_buf */
static int g_boundary;                         /* the next recorded command ends the current frame */
static Upd* g_upd;
static size_t g_nupd, g_capupd;
static uint8_t* g_updata;
static size_t g_updlen, g_updcap;

/* the GP is about to read `size` bytes at physical `addr` (before the command now being parsed) */
void gx_dff_memory(uint32_t addr, uint32_t size) {
    if (g_state != 1 && g_state != 3) return;
    addr &= 0x03FFFFFF;
    if (!size || addr + size > GUEST_RAM_SIZE) return;
    if (g_nupd == g_capupd) {
        g_capupd = g_capupd * 2 + 256;
        g_upd = realloc(g_upd, g_capupd * sizeof *g_upd);
    }
    if (g_updlen + size > g_updcap) {
        g_updcap = (g_updlen + size) * 2 + 65536;
        g_updata = realloc(g_updata, g_updcap);
    }
    if (!g_upd || !g_updata) rt_fatal("out of memory (SR_DFF)");
    g_upd[g_nupd++] = (Upd){(uint32_t)(g_len - g_fstart[g_cur]), addr, size, (uint32_t)g_updlen, (uint32_t)g_cur};
    memcpy(g_updata + g_updlen, mem_phys(addr), size);
    g_updlen += size;
}

void gx_dff_init(void) {
    const char* e = getenv("SR_DFF");
    g_target = e ? atoi(e) : -1;
    const char* c = e ? strchr(e, ',') : NULL;
    if (c) g_count = atoi(c + 1);
    if (g_count < 1) g_count = 1;
    if (g_count > MAX_FRAMES) g_count = MAX_FRAMES;
    if (g_target > 0) rt_log("[dff] will record frames %d-%d as a Dolphin FIFO log", g_target - g_count + 1, g_target);
}

static void gx_dff_append(const uint8_t* p, size_t n) {
    if (g_len + n > g_cap) {
        g_cap = (g_len + n) * 2 + 65536;
        g_buf = realloc(g_buf, g_cap);
        if (!g_buf) rt_fatal("out of memory (SR_DFF)");
    }
    memcpy(g_buf + g_len, p, n);
    g_len += n;
}

/* a BP load as FIFO bytes (the konstant colours are kept apart from BP 0xE0-0xE7 by the parser) */
static void put_bp(uint32_t v) {
    uint8_t b[5] = {0x61, (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    gx_dff_append(b, 5);
}

static void w32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void w64(FILE* f, uint64_t v) { fwrite(&v, 8, 1, f); }

static void write_file(int frame) {
    char path[64];
    snprintf(path, sizeof path, "build/frame_%d.dff", frame);
    FILE* f = fopen(path, "wb");
    if (!f) { rt_log("[dff] cannot write %s", path); return; }
    uint8_t zero[128] = {0};
    fwrite(zero, 1, 128, f);       /* header, filled in at the end */
    uint64_t frames_off = 128;
    int nf = g_cur + 1;
    for (int k = 0; k < nf; k++) fwrite(zero, 1, 64, f);  /* the FileFrameInfo list */
    uint64_t bp_off = (uint64_t)ftell(f); fwrite(g_bp, 4, 256, f);
    uint64_t cp_off = (uint64_t)ftell(f); fwrite(g_cp, 4, 256, f);
    uint64_t xfm_off = (uint64_t)ftell(f); fwrite(g_xfm, 4, 4096, f);
    uint64_t xfr_off = (uint64_t)ftell(f); fwrite(g_xfr, 4, 88, f);
    uint64_t tmem_off = (uint64_t)ftell(f); fwrite(g_tmem_copy, 1, 1 << 20, f);
    uint64_t fifo_off = (uint64_t)ftell(f); fwrite(g_buf, 1, g_len, f);
    g_fstart[nf] = g_len;
    /* all of RAM (the first frame's first update), then what the GP read, frame by frame in FIFO order */
    uint64_t ram_off = (uint64_t)ftell(f); fwrite(mem_phys(0), 1, GUEST_RAM_SIZE, f);
    uint64_t updata_off = (uint64_t)ftell(f); fwrite(g_updata, 1, g_updlen, f);
    uint8_t t[4] = {0x04, 0, 0, 0};  /* type: shown in Dolphin's UI only */
    uint64_t upd_off[MAX_FRAMES];
    uint32_t nupd[MAX_FRAMES];
    for (int k = 0; k < nf; k++) {
        upd_off[k] = (uint64_t)ftell(f);
        nupd[k] = 0;
        if (k == 0) { w32(f, 0); w32(f, 0); w64(f, ram_off); w32(f, GUEST_RAM_SIZE); fwrite(t, 1, 4, f); nupd[k]++; }
        for (size_t u = 0; u < g_nupd; u++) {
            if ((int)g_upd[u].frame != k) continue;
            w32(f, g_upd[u].pos); w32(f, g_upd[u].addr); w64(f, updata_off + g_upd[u].off); w32(f, g_upd[u].size);
            fwrite(t, 1, 4, f);
            nupd[k]++;
        }
    }
    /* the game's FIFO bounds: Dolphin's player sets up the CP FIFO there and streams the commands through it */
    void gx_pi_fifo_range(uint32_t* base, uint32_t* end);
    uint32_t fb, fe;
    gx_pi_fifo_range(&fb, &fe);
    for (int k = 0; k < nf; k++) {
        fseek(f, (long)(frames_off + 64 * (uint64_t)k), SEEK_SET);
        w64(f, fifo_off + g_fstart[k]); w32(f, (uint32_t)(g_fstart[k + 1] - g_fstart[k])); w32(f, fb); w32(f, fe);
        w64(f, upd_off[k]); w32(f, nupd[k]);
        fwrite(zero, 1, 32, f);
    }
    /* header */
    fseek(f, 0, SEEK_SET);
    w32(f, 0x0d01f1f0); w32(f, 5); w32(f, GUEST_RAM_SIZE > 0x1800000 ? 5 : 1);  /* 5: RAM larger than retail (our REL sits above 24 MB) */
    w64(f, bp_off); w32(f, 256);
    w64(f, cp_off); w32(f, 256);
    w64(f, xfm_off); w32(f, 4096);
    w64(f, xfr_off); w32(f, 88);
    w64(f, frames_off); w32(f, (uint32_t)nf);
    w32(f, 0);                     /* flags: GameCube */
    w64(f, tmem_off); w32(f, 1 << 20);
    w32(f, GUEST_RAM_SIZE); w32(f, 0);
    char id[8] = "GXEE8P";
    fwrite(id, 1, 8, f);
    fwrite(zero, 1, 24, f);
    fclose(f);
    rt_log("[dff] frames %d-%d written to %s (%zu FIFO bytes, CPU FIFO %08X-%08X, %zu memory updates, %zu KB)", frame - nf + 1,
           frame, path, g_len, fb, fe, g_nupd, g_updlen >> 10);
}

static void write_file(int frame);
/* gx_fifo.c's parser: one whole command of the immediate stream, after it ran */
void gx_dff_cmd(const uint8_t* p, size_t n) {
    if (g_skip) { g_skip = 0; return; }
    if (g_state != 1 && g_state != 3) return;
    gx_dff_append(p, n);
    if (g_boundary) {  /* an earlier frame's XFB copy: the next frame starts here */
        g_boundary = 0;
        g_fstart[++g_cur] = g_len;
    }
    if (g_state == 3) {  /* this frame's XFB copy: the end */
        write_file(g_target);
        g_state = 2;
        g_dff_active = 0;
    }
}

/* GP thread, at each XFB copy: `frames` is the number of presented frames including this one */
void gx_dff_frame_end(unsigned frames) {
    if (g_target <= 0 || g_state == 2) return;
    if (g_state == 0 && (int)frames == g_target - g_count) {
        memcpy(g_bp, g_gx.bp, sizeof g_bp);
        memcpy(g_cp, g_gx.cp, sizeof g_cp);
        memcpy(g_xfm, g_gx.xf_mem, sizeof g_xfm);
        memcpy(g_xfr, g_gx.xf_reg, sizeof g_xfr);
        g_tmem_copy = malloc(1 << 20);
        memcpy(g_tmem_copy, gx_tmem(), 1 << 20);
        g_len = 0;
        g_cur = 0;
        g_fstart[0] = 0;
        g_state = 1;
        g_skip = 1;
        g_dff_active = 1;
        for (int k = 0; k < 8; k++)  /* the konstant colours (BP 0xE0-0xE7 with the type bit) */
            put_bp((uint32_t)(0xE0 + k) << 24 | g_gx.konst[k] | 0x800000u);
        return;
    }
    if (g_state == 1 && (int)frames == g_target) g_state = 3;  /* written once this command is recorded */
    else if (g_state == 1) g_boundary = 1;
}
