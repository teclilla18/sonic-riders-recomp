/* AX microcode stand-in: the DSP side of the AX audio library, done natively.
 *
 * Every 5 ms frame the AX library hands the DSP a command list (main-RAM address, size in 16-bit
 * words). The list points at the voice parameter blocks (PBs, a linked list in main RAM); each
 * running voice is decoded from ARAM (ADPCM / PCM8 / PCM16), resampled to 32 kHz, scaled by its
 * volume envelope, optionally low-pass filtered and mixed into the main / AUX A / AUX B buses
 * (left, right, surround). The CPU gets the AUX buses for its effects and returns them; the final
 * main L/R goes to a main-RAM buffer as 16-bit stereo, which the AI DMA then plays (dsp.c,
 * audio_submit).
 *
 * The game's microcode is the one Dolphin identifies by hash 0x3DAF59B9 (a "newer" GameCube AX:
 * PBs with a low-pass filter block, the newer mixer-control bits, and the compressor command).
 * Formats and behaviour follow Dolphin's DSP HLE (AX.cpp, AXVoice.h, DSPAccelerator.cpp). The
 * polyphase resampler needs the DSP's coefficient ROM, which is not available; like Dolphin without
 * it, linear interpolation is used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

static unsigned long long g_lists;
unsigned long long ax_list_count(void) { return g_lists; }

/* ---- the parameter block (all 16-bit words, in this order) ---- */
typedef struct {
    uint16_t volume, delta;
} Vol;

typedef struct {
    uint16_t next_hi, next_lo, this_hi, this_lo;
    uint16_t src_type, coef_select, mixer_control, running, is_stream;
    Vol main_l, main_r, auxa_l, auxa_r, auxb_l, auxb_r, auxb_s, main_s, auxa_s;
    uint16_t itd[7];                       /* initial time delay (not emulated) */
    uint16_t num_updates[5], upd_hi, upd_lo;
    int16_t dpop_main_l, dpop_auxa_l, dpop_auxb_l, dpop_main_r, dpop_auxa_r, dpop_auxb_r, dpop_main_s,
        dpop_auxa_s, dpop_auxb_s;
    int16_t vol_env, vol_env_delta;
    uint16_t unknown3[3];
    uint16_t looping, sample_format, loop_hi, loop_lo, end_hi, end_lo, cur_hi, cur_lo;
    int16_t coefs[16];
    uint16_t gain, pred_scale;
    int16_t yn1, yn2;
    uint16_t ratio_hi, ratio_lo, cur_frac;
    int16_t last_samples[4];
    uint16_t loop_pred_scale, loop_yn1, loop_yn2;
    uint16_t lpf_on;
    int16_t lpf_yn1;
    uint16_t lpf_a0;
    int16_t lpf_b0;
    uint16_t loop_counter;
} PB;
#define PB_WORDS (sizeof(PB) / 2)
_Static_assert(sizeof(PB) == 98 * 2, "PB layout");

#define HILO(n) ((uint32_t)p->n##_hi << 16 | p->n##_lo)

/* PBs live in main RAM: whole-block copies with a byte swap per word */
static void read_pb(uint32_t addr, PB* pb) {
    uint16_t* w = (uint16_t*)pb;
    memcpy(w, ppc_ptr(addr), sizeof(PB));
    for (unsigned i = 0; i < PB_WORDS; i++) w[i] = __builtin_bswap16(w[i]);
}
static void write_pb(uint32_t addr, const PB* pb) {
    uint16_t w[PB_WORDS];
    for (unsigned i = 0; i < PB_WORDS; i++) w[i] = __builtin_bswap16(((const uint16_t*)pb)[i]);
    memcpy(ppc_ptr(addr), w, sizeof(PB));
}

/* ---- mixing buffers: 5 ms of 32 kHz ---- */
#define FRAME 160
enum { MAIN_L, MAIN_R, MAIN_S, AUXA_L, AUXA_R, AUXA_S, AUXB_L, AUXB_R, AUXB_S, NBUF };
static int32_t g_buf[NBUF][FRAME];
static uint32_t g_compressor_pos;

static int16_t clamp16(int64_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

/* ---- the DSP accelerator: sample reads from ARAM with ADPCM decoding and loop handling ---- */
typedef struct {
    uint32_t start, end, cur;
    uint16_t format, pred_scale;
    int16_t yn1, yn2, gain;
    int stopped;
    PB* pb;
} Acc;

static uint8_t aram8(uint32_t a) { return *aram_ptr(a); }

static uint16_t acc_current_raw(const Acc* a) {
    switch (a->format & 3) {
    case 0: { uint8_t b = aram8(a->cur >> 1); return (a->cur & 1) ? (b & 0xF) : (b >> 4); }
    case 1: return aram8(a->cur);
    case 2: return (uint16_t)(aram8(a->cur * 2) << 8 | aram8(a->cur * 2 + 1));
    default: return 0;
    }
}

static int16_t acc_read_sample(Acc* a) {
    if (a->stopped) return 0;
    int16_t raw = (int16_t)acc_current_raw(a);
    int ci = (a->pred_scale >> 4) & 7;
    int32_t c1 = a->pb->coefs[ci * 2], c2 = a->pb->coefs[ci * 2 + 1];
    int16_t val = 0;
    int step = 2;
    int decode = (a->format >> 2) & 3;
    if (decode == 0) {  /* ADPCM */
        raw &= 0xF;
        int32_t scale = 1 << (a->pred_scale & 0xF);
        if (raw >= 8) raw -= 16;
        val = clamp16((int64_t)scale * raw + ((0x400 + c1 * a->yn1 + c2 * a->yn2) >> 11));
        a->yn2 = a->yn1;
        a->yn1 = val;
        a->cur++;
        if ((a->end & 0xF) == 0 && a->cur == a->end) {
            a->cur = a->start + 1;
        } else if ((a->end & 0xF) == 1 && a->cur == a->end - 1) {
            a->cur = a->start;
        } else if ((a->cur & 15) == 0) {  /* a new 8-byte frame: its header byte is the pred/scale */
            a->pred_scale = aram8((a->cur & ~15u) >> 1) & 0x7F;
            a->cur += 2;
            step += 2;
        }
    } else {  /* PCM (8 or 16 bit), with the gain and the same predictor */
        int shift = ((a->format >> 4) & 3) == 0 ? 11 : ((a->format >> 4) & 3) == 2 ? 16 : 0;
        int32_t v = ((int32_t)a->gain * raw >> shift) + ((c1 * a->yn1) >> shift) + ((c2 * a->yn2) >> shift);
        val = (int16_t)v;
        a->yn2 = a->yn1;
        a->yn1 = val;
        if (decode != 1) a->cur++;
    }
    if (a->cur == a->end + (uint32_t)step - 1) {  /* past the end: loop or stop */
        a->cur = a->start;
        a->stopped = 1;
        PB* p = a->pb;
        if (p->looping) {
            a->pred_scale = p->loop_pred_scale & 0x7F;
            if (p->is_stream != 1) {
                a->yn1 = (int16_t)p->loop_yn1;
                a->yn2 = (int16_t)p->loop_yn2;
            } else {
                p->loop_counter++;
            }
            a->stopped = 0;  /* setting YN2 resumes reads */
        } else {
            p->running = 0;
        }
    }
    a->cur &= 0xBFFFFFFFu;
    return val;
}

/* <count> output samples at 32 kHz from the voice, linear resampling by the PB's ratio */
static void get_input(PB* p, int16_t* out, int count) {
    Acc a = {HILO(loop) & 0x3FFFFFFF, HILO(end) & 0x3FFFFFFF, HILO(cur) & 0xBFFFFFFF,
             p->sample_format, (uint16_t)(p->pred_scale & 0x7F), p->yn1, p->yn2, (int16_t)p->gain, 0, p};
    uint32_t ratio = HILO(ratio), pos = p->cur_frac;
    int16_t t[4];
    unsigned idx = 0;
    for (int k = 0; k < 4; k++) t[idx++ & 3] = p->last_samples[k];
    if (p->src_type == 2) {  /* nearest: no rate conversion */
        for (int i = 0; i < count; i++) out[i] = acc_read_sample(&a);
        for (int k = 0; k < 4; k++) p->last_samples[k] = out[count - 4 + k];
    } else {
        for (int i = 0; i < count; i++) {
            pos += ratio;
            while (pos >= 0x10000) {
                t[idx++ & 3] = acc_read_sample(&a);
                pos -= 0x10000;
            }
            uint16_t frac = (uint16_t)pos, inv = (uint16_t)-frac;
            if (frac) {
                int32_t s0 = t[idx++ & 3], s1 = t[idx++ & 3];
                out[i] = (int16_t)((s0 * inv + s1 * frac) >> 16);
                idx += 2;
            } else {
                out[i] = t[idx++ & 3];
                idx += 3;
            }
        }
        p->last_samples[3] = t[--idx & 3];
        p->last_samples[2] = t[--idx & 3];
        p->last_samples[1] = t[--idx & 3];
        p->last_samples[0] = t[--idx & 3];
    }
    p->cur_frac = (uint16_t)pos;
    p->cur_hi = (uint16_t)(a.cur >> 16);
    p->cur_lo = (uint16_t)a.cur;
    p->yn1 = a.yn1;
    p->yn2 = a.yn2;
    p->pred_scale = a.pred_scale;
}

static void mix_add(int32_t* out, const int16_t* in, int count, Vol* v, int16_t* dpop, int ramp) {
    uint16_t vol = v->volume, delta = ramp ? v->delta : 0;
    for (int i = 0; i < count; i++) {
        int16_t s = clamp16(((int64_t)in[i] * vol) >> 15);
        out[i] += s;
        vol += delta;
        *dpop = s;
    }
    v->volume = vol;
}

/* mixer-control bits of the newer GameCube ucodes */
#define MC(bit) (mc & (bit))

static void process_voice(PB* p, int32_t* const* bufs, int count) {
    if (p->running != 1) return;
    int16_t s[32];
    get_input(p, s, count);
    for (int i = 0; i < count; i++) {
        s[i] = clamp16(((int32_t)s[i] * p->vol_env) >> 15);
        p->vol_env = (int16_t)(p->vol_env + p->vol_env_delta);
    }
    if (p->lpf_on) {
        for (int i = 0; i < count; i++)
            p->lpf_yn1 = s[i] = clamp16(((int32_t)p->lpf_a0 * s[i] + (int32_t)p->lpf_b0 * p->lpf_yn1) >> 15);
    }
    uint16_t mc = p->mixer_control;
    if (MC(0x0001)) mix_add(bufs[MAIN_L], s, count, &p->main_l, &p->dpop_main_l, MC(0x0008) != 0);
    if (MC(0x0002)) mix_add(bufs[MAIN_R], s, count, &p->main_r, &p->dpop_main_r, MC(0x0008) != 0);
    if (MC(0x0004)) mix_add(bufs[MAIN_S], s, count, &p->main_s, &p->dpop_main_s, MC(0x0008) != 0);
    if (MC(0x0010)) mix_add(bufs[AUXA_L], s, count, &p->auxa_l, &p->dpop_auxa_l, MC(0x0040) != 0);
    if (MC(0x0020)) mix_add(bufs[AUXA_R], s, count, &p->auxa_r, &p->dpop_auxa_r, MC(0x0040) != 0);
    if (MC(0x0080)) mix_add(bufs[AUXA_S], s, count, &p->auxa_s, &p->dpop_auxa_s, MC(0x0100) != 0);
    if (MC(0x0200)) mix_add(bufs[AUXB_L], s, count, &p->auxb_l, &p->dpop_auxb_l, MC(0x0800) != 0);
    if (MC(0x0400)) mix_add(bufs[AUXB_R], s, count, &p->auxb_r, &p->dpop_auxb_r, MC(0x0800) != 0);
    if (MC(0x1000)) mix_add(bufs[AUXB_S], s, count, &p->auxb_s, &p->dpop_auxb_s, MC(0x2000) != 0);
}

/* per-millisecond parameter updates (pb word offset, value), read from main RAM */
static void apply_updates(PB* p, int ms, const uint16_t* upd) {
    unsigned start = 0;
    for (int i = 0; i < ms; i++) start += p->num_updates[i];
    unsigned n = p->num_updates[ms];
    if (start >= 32 || n > 32 - start) return;
    for (unsigned i = start; i < start + n; i++) {
        uint16_t off = upd[2 * i], val = upd[2 * i + 1];
        if (off < PB_WORDS) ((uint16_t*)p)[off] = val;
    }
}

static int g_axlog = -1;  /* SR_AXLOG=1: dump the running voices every 400 frames (2 s) */
static void process_pb_list(uint32_t addr) {
    int guard = 0;
    if (g_axlog < 0) g_axlog = getenv("SR_AXLOG") != NULL;
    int dump = g_axlog && g_lists % 400 == 1;
    while (addr && guard++ < 256) {
        PB pb, orig;
        read_pb(addr, &pb);
        orig = pb;
        if (dump && pb.running == 1)
            rt_log("[axv] pb %08X fmt %04X src %u ratio %04X.%04X loop %u stream %u cur %08X end %08X loopaddr %08X "
                   "mc %04X vol %d gain %04X ps %02X main %04X/%04X lpf %u", addr, pb.sample_format, pb.src_type,
                   pb.ratio_hi, pb.ratio_lo, pb.looping, pb.is_stream, (uint32_t)pb.cur_hi << 16 | pb.cur_lo,
                   (uint32_t)pb.end_hi << 16 | pb.end_lo, (uint32_t)pb.loop_hi << 16 | pb.loop_lo, pb.mixer_control,
                   pb.vol_env, pb.gain, pb.pred_scale, pb.main_l.volume, pb.main_r.volume, pb.lpf_on);
        uint16_t upd[64];
        int any_upd = pb.num_updates[0] | pb.num_updates[1] | pb.num_updates[2] | pb.num_updates[3] | pb.num_updates[4];
        if (any_upd) {
            uint32_t ua = (uint32_t)pb.upd_hi << 16 | pb.upd_lo;
            for (int i = 0; i < 64; i++) upd[i] = ua ? ld16(ua + 2 * i) : 0;
        }
        if (!any_upd && pb.running != 1) {  /* a stopped voice with no updates: nothing to do */
            addr = (uint32_t)pb.next_hi << 16 | pb.next_lo;
            continue;
        }
        for (int ms = 0; ms < 5; ms++) {
            if (any_upd) apply_updates(&pb, ms, upd);
            int32_t* bufs[NBUF];
            for (int b = 0; b < NBUF; b++) bufs[b] = g_buf[b] + ms * 32;
            process_voice(&pb, bufs, 32);
        }
        if (memcmp(&pb, &orig, sizeof pb)) write_pb(addr, &pb);
        addr = (uint32_t)pb.next_hi << 16 | pb.next_lo;
    }
}

/* ---- buffer transfers with main RAM (32-bit big-endian samples) ---- */
static void upload(uint32_t addr, const int32_t* b) {
    for (int i = 0; i < FRAME; i++) st32(addr + 4 * i, (uint32_t)b[i]);
}
static void download_add(uint32_t addr, int32_t* b) {
    for (int i = 0; i < FRAME; i++) b[i] += (int32_t)ld32(addr + 4 * i);
}

static void setup(uint32_t addr) {
    for (int b = 0; b < NBUF; b++) {
        int32_t v = (int32_t)((uint32_t)ld16(addr + 6 * b) << 16 | ld16(addr + 6 * b + 2));
        int16_t d = (int16_t)ld16(addr + 6 * b + 4);
        for (int i = 0; i < FRAME; i++) g_buf[b][i] = v ? v + i * d : 0;
    }
}

static void compressor(uint16_t threshold, uint16_t frames, uint32_t table) {
    int hit = 0;
    for (int i = 0; i < FRAME && !hit; i++)
        if (abs(g_buf[MAIN_L][i]) > threshold || abs(g_buf[MAIN_R][i]) > threshold) hit = 1;
    uint32_t off;
    if (hit) {
        off = g_compressor_pos * FRAME * 2;
        g_compressor_pos = frames;
    } else if (g_compressor_pos) {
        g_compressor_pos--;
        off = (11 + g_compressor_pos) * FRAME * 2;
    } else {
        return;
    }
    for (int i = 0; i < FRAME; i++) {
        uint16_t c = ld16(table + off + 2 * i);
        g_buf[MAIN_L][i] = (int32_t)(((int64_t)g_buf[MAIN_L][i] * c) >> 15);
        g_buf[MAIN_R][i] = (int32_t)(((int64_t)g_buf[MAIN_R][i] * c) >> 15);
    }
}

static void output(uint32_t surround_addr, uint32_t lr_addr) {
    upload(surround_addr, g_buf[MAIN_S]);
    int16_t host[FRAME * 2];
    for (int i = 0; i < FRAME; i++) {  /* main RAM: interleaved right, left (what the AI DMA plays) */
        int16_t l = clamp16(g_buf[MAIN_L][i]), r = clamp16(g_buf[MAIN_R][i]);
        st16(lr_addr + 4 * i, (uint16_t)r);
        st16(lr_addr + 4 * i + 2, (uint16_t)l);
        host[2 * i] = l;
        host[2 * i + 1] = r;
    }
    /* the host plays each mixed frame once, in order: taking it from the AI DMA address instead
     * repeated or skipped frames whenever the game's AI interrupt handler (which sets the next
     * address) ran late relative to the host's audio clock */
    audio_frame(host, FRAME);
}

void ax_process_command_list(uint32_t addr, uint32_t size) {
    if (g_lists++ == 0) rt_log("[ax] first command list at %08X (%u words)", addr, size);
    /* SR_AUDIO=0 turns the mixer off (the lists are then only counted). Note that the game needs it:
     * story cutscenes wait for their voices to end (a voice the DSP never stops never ends). */
    static int enabled = -1;
    if (enabled < 0) {
        const char* a = getenv("SR_AUDIO");
        enabled = !(a && a[0] == '0');
        if (!enabled) rt_log("[ax] mixer off (SR_AUDIO=0)");
    }
    if (!enabled) return;
    {   /* SR_ARAMDUMP=<file>: ARAM 0-64 KB once, at the 3000th list (debugging) */
        static int done;
        static const char* d = (const char*)1;
        if (d == (const char*)1) d = getenv("SR_ARAMDUMP");
        if (d && !done && g_lists == 3000) {
            done = 1;
            FILE* f = fopen(d, "wb");
            if (f) { fwrite(aram_ptr(0), 1, 0x10000, f); fclose(f); rt_log("[ax] ARAM dumped to %s", d); }
        }
    }
    uint32_t pc = addr, pb_addr = 0;
    int guard = 0;
#define W() (pc += 2, ld16(pc - 2))
#define W32() (pc += 4, ld32(pc - 4))
    for (;;) {
        if (guard++ > 4096) break;
        uint16_t cmd = W();
        switch (cmd) {
        case 0x00: setup(W32()); break;
        case 0x01: {  /* download + mix with volume (main, AUX A, AUX B; L R S each) */
            uint32_t a = W32();
            uint16_t vol[3];
            vol[0] = W();
            vol[1] = W();
            vol[2] = W();
            for (int g = 0; g < 3; g++)
                for (int c = 0; c < 3; c++) {
                    int32_t* b = g_buf[g * 3 + c];
                    for (int i = 0; i < FRAME; i++, a += 4)
                        b[i] += (int32_t)(((int64_t)(int32_t)ld32(a) * vol[g]) >> 15);
                }
            break;
        }
        case 0x02: pb_addr = W32(); break;
        case 0x03: process_pb_list(pb_addr); break;
        case 0x04: case 0x05: {  /* AUX A/B: send the bus to the CPU, add its return to main */
            uint32_t wr = W32(), rd = W32();
            int base = cmd == 0x04 ? AUXA_L : AUXB_L;
            if (wr)
                for (int c = 0; c < 3; c++) upload(wr + c * FRAME * 4, g_buf[base + c]);
            for (int c = 0; c < 3; c++) download_add(rd + c * FRAME * 4, g_buf[MAIN_L + c]);
            break;
        }
        case 0x06: {  /* upload main L R S */
            uint32_t a = W32();
            for (int c = 0; c < 3; c++) upload(a + c * FRAME * 4, g_buf[MAIN_L + c]);
            break;
        }
        case 0x07: {  /* set main L = R = buffer */
            uint32_t a = W32();
            for (int i = 0; i < FRAME; i++) {
                int32_t v = (int32_t)ld32(a + 4 * i);
                g_buf[MAIN_L][i] = g_buf[MAIN_R][i] = v;
                g_buf[MAIN_S][i] = 0;
            }
            break;
        }
        case 0x08: pc += 20; break;
        case 0x09: {  /* AUX B without sending it */
            uint32_t rd = W32();
            for (int c = 0; c < 3; c++) download_add(rd + c * FRAME * 4, g_buf[MAIN_L + c]);
            break;
        }
        case 0x0A: case 0x0B: case 0x0C: break;
        case 0x0D: {  /* continue in another list */
            uint32_t a = W32();
            (void)W();
            pc = a;
            break;
        }
        case 0x0E: {
            uint32_t s = W32(), lr = W32();
            output(s, lr);
            break;
        }
        case 0x0F: return;
        case 0x10: {  /* AUX B L/R: send, then main += returned and AUX B = returned */
            uint32_t ul = W32(), dl = W32();
            upload(ul, g_buf[AUXB_L]);
            upload(ul + FRAME * 4, g_buf[AUXB_R]);
            for (int c = 0; c < 2; c++)
                for (int i = 0; i < FRAME; i++) {
                    int32_t v = (int32_t)ld32(dl + (c * FRAME + i) * 4);
                    g_buf[AUXB_L + c][i] = v;
                    g_buf[MAIN_L + c][i] += v;
                }
            break;
        }
        case 0x11: {  /* opposite L/R */
            uint32_t a = W32();
            for (int i = 0; i < FRAME; i++) {
                int32_t v = (int32_t)ld32(a + 4 * i);
                g_buf[MAIN_L][i] = -v;
                g_buf[MAIN_R][i] = v;
                g_buf[MAIN_S][i] = 0;
            }
            break;
        }
        case 0x12: {
            uint16_t thr = W(), frames = W();
            compressor(thr, frames, W32());
            break;
        }
        case 0x13: {  /* send AUX A LRS and AUX B S; download into main L/R and AUX B L/R */
            uint32_t auxa = W32(), auxbs = W32(), ml = W32(), mr = W32(), bl = W32(), br = W32();
            for (int c = 0; c < 3; c++) upload(auxa + c * FRAME * 4, g_buf[AUXA_L + c]);
            upload(auxbs, g_buf[AUXB_S]);
            download_add(ml, g_buf[MAIN_L]);
            download_add(mr, g_buf[MAIN_R]);
            download_add(bl, g_buf[AUXB_L]);
            download_add(br, g_buf[AUXB_R]);
            break;
        }
        default: {
            static int logged;
            if (logged++ < 8) rt_log("[ax] unknown command %04X in the list at %08X", cmd, addr);
            return;
        }
        }
    }
#undef W
#undef W32
}
