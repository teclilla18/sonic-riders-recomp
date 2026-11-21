/* Songs in common formats (MP3, OGG, FLAC, WAV) turned into the game's own music format, once, for music packs
 * (music.c). The game then plays them with its own ADX player: looping, volume and pausing as for its own music.
 *
 * Output: ADX version 3 (CRI 4-bit ADPCM, 18-byte frames of 32 samples per channel, 500 Hz prediction filter),
 * 48 kHz stereo with a loop, the layout of the game's stage songs (header 0x330 bytes + "(c)CRI", data at 0x334).
 * The loop is the whole song, or the song's own loop points from OGG tags (LOOPSTART with LOOPLENGTH or LOOPEND,
 * in samples: the convention of RPG Maker and many game rips).
 * Decoders: dr_mp3, dr_flac, dr_wav (public domain / MIT-0, mackron/dr_libs) and stb_vorbis (public domain). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "third_party/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_wav.h"
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/stb_vorbis.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define OUT_RATE 48000
#define DATA_AT 0x334

static int ext_is(const char* p, const char* e) {
    size_t n = strlen(p), k = strlen(e);
    if (n < k) return 0;
    for (size_t i = 0; i < k; i++)
        if ((p[n - k + i] | 0x20) != e[i]) return 0;
    return 1;
}

/* decoded song: interleaved int16, `ch` channels */
typedef struct { int16_t* pcm; uint64_t frames; unsigned ch, rate; int64_t loop_start, loop_end; } Pcm;

static void ogg_loop_tags(stb_vorbis* v, Pcm* p) {
    stb_vorbis_comment c = stb_vorbis_get_comment(v);
    int64_t start = -1, len = -1, end = -1;
    for (int i = 0; i < c.comment_list_length; i++) {
        const char* s = c.comment_list[i];
        if (!strncmp(s, "LOOPSTART=", 10) || !strncmp(s, "loopstart=", 10)) start = atoll(s + 10);
        else if (!strncmp(s, "LOOPLENGTH=", 11) || !strncmp(s, "looplength=", 11)) len = atoll(s + 11);
        else if (!strncmp(s, "LOOPEND=", 8) || !strncmp(s, "loopend=", 8)) end = atoll(s + 8);
    }
    if (start >= 0) {
        p->loop_start = start;
        p->loop_end = len > 0 ? start + len : end > start ? end : -1;
    }
}

static int decode(const char* path, Pcm* p) {
    memset(p, 0, sizeof *p);
    p->loop_start = p->loop_end = -1;
    if (ext_is(path, ".mp3")) {
        drmp3_config cfg;
        drmp3_uint64 n = 0;
        p->pcm = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &n, NULL);
        p->frames = n, p->ch = cfg.channels, p->rate = cfg.sampleRate;
    } else if (ext_is(path, ".flac")) {
        unsigned int ch = 0, rate = 0;
        drflac_uint64 n = 0;
        p->pcm = drflac_open_file_and_read_pcm_frames_s16(path, &ch, &rate, &n, NULL);
        p->frames = n, p->ch = ch, p->rate = rate;
    } else if (ext_is(path, ".wav")) {
        unsigned int ch = 0, rate = 0;
        drwav_uint64 n = 0;
        p->pcm = drwav_open_file_and_read_pcm_frames_s16(path, &ch, &rate, &n, NULL);
        p->frames = n, p->ch = ch, p->rate = rate;
    } else if (ext_is(path, ".ogg")) {
        int err = 0;
        stb_vorbis* v = stb_vorbis_open_filename(path, &err, NULL);
        if (!v) return 0;
        stb_vorbis_info in = stb_vorbis_get_info(v);
        p->ch = (unsigned)in.channels, p->rate = in.sample_rate;
        ogg_loop_tags(v, p);
        uint64_t cap = 1 << 20, n = 0;
        p->pcm = malloc(cap * p->ch * sizeof(int16_t));
        for (;;) {
            if (n + 4096 > cap) {
                cap *= 2;
                int16_t* g = realloc(p->pcm, cap * p->ch * sizeof(int16_t));
                if (!g) break;
                p->pcm = g;
            }
            int got = stb_vorbis_get_samples_short_interleaved(v, (int)p->ch, p->pcm + n * p->ch, 4096 * (int)p->ch);
            if (got <= 0) break;
            n += (uint64_t)got;
        }
        p->frames = n;
        stb_vorbis_close(v);
    }
    return p->pcm && p->frames && p->ch && p->rate;
}

static void pcm_free(void* m) { free(m); }  /* the dr_ and stb decoders allocate with malloc */

/* stereo at 48 kHz (linear interpolation; songs at 44.1/48 kHz lose nothing audible), loop points scaled */
static int16_t* to_stereo48(const Pcm* p, uint64_t* out_frames) {
    double step = (double)p->rate / OUT_RATE;
    uint64_t n = (uint64_t)((double)p->frames / step);
    int16_t* o = malloc((n + 1) * 2 * sizeof(int16_t));
    if (!o) return NULL;
    for (uint64_t i = 0; i < n; i++) {
        double x = i * step;
        uint64_t a = (uint64_t)x;
        uint64_t b = a + 1 < p->frames ? a + 1 : a;
        double f = x - (double)a;
        for (unsigned c = 0; c < 2; c++) {
            unsigned sc = c < p->ch ? c : 0;
            double v = p->pcm[a * p->ch + sc] * (1 - f) + p->pcm[b * p->ch + sc] * f;
            o[i * 2 + c] = (int16_t)lrint(v);
        }
    }
    *out_frames = n;
    return o;
}

static void wbe16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void wbe32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }

/* one 18-byte frame of 32 samples of one channel; closed loop: the history is what the decoder will rebuild */
static void adx_frame(uint8_t* out, const int16_t* s, int stride, int c1, int c2, int* h1, int* h2) {
    int d[32], mx = 0, mn = 0, p1 = *h1, p2 = *h2;
    for (int i = 0; i < 32; i++) {  /* open-loop pass for the scale */
        int pred = (c1 * p1 + c2 * p2) >> 12;
        d[i] = s[i * stride] - pred;
        if (d[i] > mx) mx = d[i];
        if (d[i] < mn) mn = d[i];
        p2 = p1, p1 = s[i * stride];
    }
    int scale = mx / 7 > -mn / 8 ? mx / 7 : -mn / 8;
    if (scale < 1) scale = 1;
    if (scale > 0x1FFF) scale = 0x1FFF;
    wbe16(out, (uint32_t)scale);
    memset(out + 2, 0, 16);
    p1 = *h1, p2 = *h2;
    for (int i = 0; i < 32; i++) {
        int pred = (c1 * p1 + c2 * p2) >> 12;
        int e = s[i * stride] - pred;
        int q = (int)lrint((double)e / scale);
        if (q > 7) q = 7;
        if (q < -8) q = -8;
        int r = q * scale + pred;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        p2 = p1, p1 = r;
        out[2 + i / 2] |= (uint8_t)((q & 15) << (i & 1 ? 0 : 4));
    }
    *h1 = p1, *h2 = p2;
}

/* src (MP3/OGG/FLAC/WAV) -> dst (.adx); 1 on success */
int music_convert(const char* src, const char* dst) {
    uint64_t t0 = platform_now_ns();
    Pcm p;
    if (!decode(src, &p)) {
        rt_log("[music] cannot read %s (not an MP3, OGG, FLAC or WAV file it can decode)", src);
        if (p.pcm) pcm_free(p.pcm);
        return 0;
    }
    uint64_t n = 0;
    int16_t* s = to_stereo48(&p, &n);
    double k = (double)OUT_RATE / p.rate;
    int64_t ls = p.loop_start >= 0 ? (int64_t)(p.loop_start * k) : 0;
    int64_t le = p.loop_end > 0 ? (int64_t)(p.loop_end * k) : (int64_t)n;
    pcm_free(p.pcm);
    if (!s || n < 32) { free(s); return 0; }
    if (le > (int64_t)n) le = (int64_t)n;
    ls &= ~31;  /* the loop starts on a frame */
    if (ls >= le) ls = 0;
    uint64_t frames = (n + 31) / 32;
    size_t size = DATA_AT + frames * 36 + 18;
    uint8_t* o = calloc(1, size);
    if (!o) { free(s); return 0; }
    /* header: as the game's own stage songs */
    o[0] = 0x80, o[1] = 0x00;
    wbe16(o + 2, DATA_AT - 4);
    o[4] = 3, o[5] = 18, o[6] = 4, o[7] = 2;
    wbe32(o + 8, OUT_RATE);
    wbe32(o + 12, (uint32_t)n);
    wbe16(o + 16, 500);
    o[18] = 3, o[19] = 0;
    wbe16(o + 0x14, 0);
    wbe16(o + 0x16, 1);
    wbe32(o + 0x18, 1);
    wbe32(o + 0x1C, (uint32_t)ls);
    wbe32(o + 0x20, (uint32_t)(DATA_AT + (ls / 32) * 36));
    wbe32(o + 0x24, (uint32_t)le);
    wbe32(o + 0x28, (uint32_t)(DATA_AT + ((le + 31) / 32) * 36));
    memcpy(o + DATA_AT - 6, "(c)CRI", 6);
    /* prediction coefficients for a 500 Hz cutoff at 48 kHz (12-bit fixed point) */
    double a = sqrt(2.0) - cos(2.0 * 3.14159265358979323846 * 500.0 / OUT_RATE), b = sqrt(2.0) - 1.0;
    double c = (a - sqrt((a + b) * (a - b))) / b;
    int c1 = (int)lrint(c * 2.0 * 4096.0), c2 = (int)lrint(-(c * c) * 4096.0);
    int h[2][2] = {{0, 0}, {0, 0}};
    int16_t blk[64];
    for (uint64_t f = 0; f < frames; f++) {
        for (int i = 0; i < 32; i++) {
            uint64_t at = f * 32 + (uint64_t)i;
            blk[i * 2] = at < n ? s[at * 2] : 0;
            blk[i * 2 + 1] = at < n ? s[at * 2 + 1] : 0;
        }
        for (int ch = 0; ch < 2; ch++)
            adx_frame(o + DATA_AT + f * 36 + ch * 18, blk + ch, 2, c1, c2, &h[ch][0], &h[ch][1]);
    }
    uint8_t* end = o + DATA_AT + frames * 36;  /* end marker */
    end[0] = 0x80, end[1] = 0x01, wbe16(end + 2, 14);
    free(s);
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.part", dst);
    FILE* f = fopen(tmp, "wb");
    int ok = f && fwrite(o, 1, size, f) == size;
    if (f) ok = fclose(f) == 0 && ok;
    free(o);
    if (ok) {
        remove(dst);
        ok = rename(tmp, dst) == 0;
    }
    if (!ok) {
        remove(tmp);
        rt_log("[music] cannot write %s", dst);
        return 0;
    }
    rt_log("[music] converted %s: %.1f s of music, %u Hz %u ch -> ADX 48 kHz stereo%s, in %.1f s", src, n / (double)OUT_RATE,
           p.rate, p.ch, p.loop_start >= 0 ? " (its own loop points)" : "", (platform_now_ns() - t0) / 1e9);
    return 1;
}
