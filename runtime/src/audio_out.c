/* The AI DMA "played" a block of the game's output buffer: convert it to host 16-bit stereo (left,
 * right) and hand it to the platform's audio device (platform_audio_push, 32 kHz).
 * The buffer holds big-endian 16-bit pairs, right channel first (as the AX microcode writes them).
 * SR_WAVOUT=<file> also records everything to a WAV file (testing without listening). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "settings.h"

/* ---- Super Sonic's held boost, "like Need for Speed nitrous" (the user, 2026-11-20): with the speed view's
 * strength b (0..1, boost_hold_audio), the whole mix is muffled (a low-pass that closes from 16 kHz to about
 * 1.5 kHz) and driven into a soft clip whose drive rises with b squared, so the grit builds slowly and then
 * rushes near the top. b comes on at once with a held Super boost and goes the moment it ends (boost_hold.c);
 * b = 0: the mix is passed through untouched. */
#define NOS_RATE      32000.0
#define NOS_FC_OPEN   16000.0
#define NOS_FC_SHUT   1500.0
#define NOS_DRIVE     7.0       /* extra pre-gain at full strength */
#define NOS_WET       0.75      /* the clipped share at full strength */
static double g_nos_lp[2];      /* the low-pass state per channel */
static int g_nos_on;

static double g_nos_b;          /* the strength in use: follows boost_hold_audio within ~15 ms (no clicks) */

static void nos_process(int16_t* lr, unsigned frames, double want) {
    g_nos_b += (want - g_nos_b) * 0.35;  /* per 5 ms mix */
    if (want <= 0.0 && g_nos_b < 0.01) g_nos_b = 0.0;
    double b = g_nos_b;
    if (b <= 0.0005) { g_nos_on = 0; return; }
    if (!g_nos_on) {  /* start the filter at the signal: no click */
        g_nos_on = 1;
        g_nos_lp[0] = lr[0] / 32768.0;
        g_nos_lp[1] = lr[1] / 32768.0;
    }
    double fc = NOS_FC_OPEN * pow(NOS_FC_SHUT / NOS_FC_OPEN, b);  /* even steps in pitch */
    double a = 1.0 - exp(-2.0 * 3.14159265358979 * fc / NOS_RATE);
    double drive = 1.0 + NOS_DRIVE * b * b, norm = tanh(drive), wet = NOS_WET * b;
    for (unsigned i = 0; i < frames * 2; i++) {
        double x = lr[i] / 32768.0;
        double* st = &g_nos_lp[i & 1];
        *st += (x - *st) * a;
        double y = *st;
        y = y + (tanh(drive * y) / norm - y) * wet;
        y *= 1.0 - 0.15 * b;  /* the clip raises quiet sounds: keep the loudness about even */
        int v = (int)lrint(y * 32767.0);
        lr[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
}

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
    static int16_t nos_buf[2 * 2048];
    double b = boost_hold_audio();
    if ((b > 0.0005 || g_nos_on || g_nos_b > 0.0) && frames <= 2048) {
        memcpy(nos_buf, lr, frames * 4u);
        nos_process(nos_buf, frames, b);
        lr = nos_buf;
    }
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
