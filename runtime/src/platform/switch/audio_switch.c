/* Switch audio output: audout (48 kHz stereo PCM16). The game's 32 kHz is resampled 2:3 with linear
 * interpolation. Blocks of 10 ms go through a ring of buffers; when none has been released (the
 * output is behind), the block is dropped instead of waiting, so the AI clock (dsp.c, its own thread)
 * keeps the game's pace. Called with each mixed AX frame (ax.c, on the game thread). */
#include <malloc.h>
#include <string.h>
#include <switch.h>

#include "runtime.h"

#define OUT_RATE 48000
#define BLOCK 480        /* 10 ms at 48 kHz */
#define NBUF 6

static AudioOutBuffer g_bufs[NBUF];
static int g_free[NBUF], g_nfree;
static int g_state;      /* 0 not started, 1 running, -1 failed */
static int16_t* g_fill;  /* the block being filled */
static int g_fill_idx = -1, g_fill_n;
static int16_t g_last_l, g_last_r;
static uint32_t g_phase;  /* 32 kHz -> 48 kHz position, 16.16 */

static int start(void) {
    if (R_FAILED(audoutInitialize())) return -1;
    if (R_FAILED(audoutStartAudioOut())) return -1;
    size_t size = (BLOCK * 4 + 0xFFF) & ~0xFFFu;
    for (int i = 0; i < NBUF; i++) {
        void* p = memalign(0x1000, size);
        if (!p) return -1;
        memset(p, 0, size);
        g_bufs[i].next = NULL;
        g_bufs[i].buffer = p;
        g_bufs[i].buffer_size = size;
        g_bufs[i].data_size = BLOCK * 4;
        g_bufs[i].data_offset = 0;
        g_free[g_nfree++] = i;
    }
    rt_log("[audio] audout at %u Hz, %u channels", audoutGetSampleRate(), audoutGetChannelCount());
    return 1;
}

static void reclaim(void) {
    AudioOutBuffer* rel = NULL;
    u32 n = 0;
    while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&rel, &n)) && n && rel) {
        int i = (int)(rel - g_bufs);
        if (i >= 0 && i < NBUF && g_nfree < NBUF) g_free[g_nfree++] = i;
        rel = NULL;
        n = 0;
    }
}

static void put(int16_t l, int16_t r) {
    if (g_fill_idx < 0) {
        if (!g_nfree) reclaim();
        if (!g_nfree) return;  /* output behind: drop */
        g_fill_idx = g_free[--g_nfree];
        g_fill = (int16_t*)g_bufs[g_fill_idx].buffer;
        g_fill_n = 0;
    }
    g_fill[2 * g_fill_n] = l;
    g_fill[2 * g_fill_n + 1] = r;
    if (++g_fill_n == BLOCK) {
        armDCacheFlush(g_fill, BLOCK * 4);
        audoutAppendAudioOutBuffer(&g_bufs[g_fill_idx]);
        g_fill_idx = -1;
    }
}

void platform_audio_push(const int16_t* lr, unsigned frames) {
    if (g_state == 0) g_state = start();
    if (g_state < 0) return;
    reclaim();
    /* each input frame advances 2/3 of an output frame: emit outputs at positions between the last
     * sample and this one (linear interpolation) */
    for (unsigned i = 0; i < frames; i++) {
        int16_t l = lr[2 * i], r = lr[2 * i + 1];
        while (g_phase < 0x10000) {
            int32_t f = (int32_t)g_phase;
            put((int16_t)(g_last_l + (((int32_t)l - g_last_l) * f >> 16)), (int16_t)(g_last_r + (((int32_t)r - g_last_r) * f >> 16)));
            g_phase += 0x10000 * 2 / 3;
        }
        g_phase -= 0x10000;
        g_last_l = l;
        g_last_r = r;
    }
}
