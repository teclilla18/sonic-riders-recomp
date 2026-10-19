/* The AI DMA "played" a block of the game's output buffer: convert it to host 16-bit stereo (left,
 * right) and hand it to the platform's audio device (platform_audio_push, 32 kHz).
 * The buffer holds big-endian 16-bit pairs, right channel first (as the AX microcode writes them).
 * SR_WAVOUT=<file> also records everything to a WAV file (testing without listening). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

static FILE* g_wav;
static uint32_t g_wav_bytes;
static int g_wav_init;

static void wav_header(FILE* f, uint32_t data_bytes) {
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + data_bytes;
    memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);       /* PCM */
    s = 2; memcpy(h + 22, &s, 2);                /* stereo */
    v = 32000; memcpy(h + 24, &v, 4);
    v = 32000 * 4; memcpy(h + 28, &v, 4);
    s = 4; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fseek(f, 0, SEEK_END);
}

/* the AI DMA played a block: the host output takes the frames from the mixer instead (audio_frame) */
void audio_submit(uint32_t addr, uint32_t bytes) { (void)addr; (void)bytes; }

/* one mixed AX frame, 32 kHz stereo (left, right) */
void audio_frame(const int16_t* lr, unsigned frames) {
    platform_audio_push(lr, frames);
    if (!g_wav_init) {
        g_wav_init = 1;
        const char* p = getenv("SR_WAVOUT");
        if (p && (g_wav = fopen(p, "wb")) != NULL) {
            wav_header(g_wav, 0);
            rt_log("[audio] recording to %s", p);
        }
    }
    if (g_wav) {
        fwrite(lr, 4, frames, g_wav);
        g_wav_bytes += frames * 4;
        if ((g_wav_bytes & 0x3FFFF) < frames * 4) {  /* keep the header current (~every 2 s) */
            wav_header(g_wav, g_wav_bytes);
            fflush(g_wav);
        }
    }
}
