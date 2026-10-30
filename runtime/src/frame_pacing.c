/* Frame rate modes (settings "Frame rate"): keep the game at full speed when a frame takes longer than
 * one 60 Hz retrace.
 *
 * Sonic Riders advances its simulation one step per loop iteration, and an iteration normally ends by
 * waiting for the next retrace and presenting (fn_8000610C). A frame that needs 17 ms misses its
 * retrace and waits for the one after, so the game runs at 30 fps and half speed.
 *
 * The game has its own unused 30 fps mode, which this file drives (NOTES.md "Frame rate modes"):
 *   - GAME_FRAME_MODE (0x8021BB7C) != 0 turns it on. GAME_FRAME_SKIP (0x8021BB80) is then toggled by
 *     the frame end; while it is non-zero the game's draw functions return early (several hundred
 *     `if (skip) return` sites), and the frame end returns without presenting or waiting.
 *   - The next iteration draws, waits for two retraces after the previous present, and presents.
 *   - GAME_FRAME_STEPS (0x8021BBF4, float) = iterations per drawn frame (mode + 1); effect and
 *     animation code that only runs on drawn frames multiplies its step by it.
 * Logic runs on every iteration either way, so the game keeps 60 steps a second.
 *
 * Modes:
 *   FR_60   "60 fps": drawn and presented as Auto (below) while the console keeps up. When frames keep
 *           coming late (S30_TROUBLE times within 60 retraces, not counting loads), it switches to the
 *           game's steady 30 fps mode for the busy section, and back to 60 after a hold (3 s, doubled
 *           each time 60 failed again soon, up to 30 s) once the measured work fits a retrace: the
 *           game thread's work per drawn iteration, the GP thread's per drawn frame and the GPU's time
 *           per frame all under S30_FIT_MS. The game never slows down. (Until 2026-10 this mode was the
 *           original behaviour, slow motion when late: on a console at stock clocks that was unplayable.)
 *   FR_30   the game's own 30 fps mode as it is: every second iteration skips drawing.
 *   FR_AUTO "keep game speed": draws every step while the game keeps up; when it falls behind the
 *           retrace schedule, the next iteration skips drawing (at most FR_MAX_SKIPS in a row), and
 *           a frame that finishes after its retrace is presented at once instead of waiting for the
 *           next retrace (VIWaitForRetrace skipped), as WWHD's "flip when ready" does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "game_addrs.h"
#include "settings.h"

#define GAME_FRAME_MODE   0x8021BB7Cu  /* u32: the game's 30 fps mode (0 off) */
#define GAME_FRAME_SKIP   0x8021BB80u  /* u32: this iteration does not draw */
#define GAME_FRAME_STEPS  0x8021BBF4u  /* f32: iterations per drawn frame */
#define GAME_FRAME_ADV    0x8021B8D4u  /* u32: frame counter advance (the frame end skips the step update while 0) */
#define GAME_SPEED_SCALE  0x8021B8ECu  /* f32: the game's speed scale (scene resets set steps = (mode+1) * scale) */

#define FR_MAX_SKIPS 2   /* Auto: undrawn iterations in a row at most (20 fps drawn at worst) */
#define FR_REBASE 8      /* Auto: further behind than this many retraces (a load, a hitch): restart the schedule */
#define S30_TROUBLE 4     /* 60 fps: late presents + undrawn iterations within 60 retraces that switch to 30 */
#define S30_FIT_MS 13.5   /* 60 fps: work per frame that leaves room in a 16.7 ms retrace */
#define S30_HOLD_MIN 180  /* retraces at 30 before 60 is tried again (3 s) */
#define S30_HOLD_MAX 1800 /* 30 s: the hold doubles each time 60 failed within S30_SHORT */
#define S30_SHORT 600     /* 10 s */

void f_8000610C_orig(Cpu* c);  /* the frame end (generated) */
void f_80017038_orig(Cpu* c);  /* VIWaitForRetrace */

static uint32_t g_present_now; /* guest thread whose frame end skips VIWaitForRetrace (0: none). Other
                                * threads (CRI, sound) wait for retraces in loops: they must never skip */
static uint32_t g_base;        /* Auto: retrace count at which step 0 was due */
static uint32_t g_steps;       /* Auto: iterations since g_base */
static int g_skips_in_row;
static int g_prev_mode = -1;
static uint64_t g_t_exit;                  /* when the previous frame end returned */
static double g_busy_ms = -1.0;            /* game-thread work of a drawn iteration, smoothed */
static double g_gp_ms = -1.0;              /* GP thread work per drawn frame, smoothed */
static uint64_t g_gp_prev;
/* 60 fps mode's steady-30 state */
static int g_s30;
static uint32_t g_s30_since, g_s30_hold = S30_HOLD_MIN, g_win_field;
static int g_win_trouble, g_win_rebase;
static unsigned g_n_s30_iter, g_n_s30_switch, g_n_s30_total;
/* statistics for the [pace] log line */
static unsigned g_n_iter, g_n_drawn, g_n_late, g_n_rebase;
static uint32_t g_stat_field;

static void set_steps(float n) {
    if (ld32(GAME_FRAME_ADV) == 0) return;  /* as the game's frame end: no update while paused */
    float scale = u32_as_f32(ld32(GAME_SPEED_SCALE));
    if (!(scale > 0.0f && scale < 8.0f)) scale = 1.0f;
    st32(GAME_FRAME_STEPS, f32_as_u32(n * scale));
}

static uint32_t g_main_thread;  /* the guest thread that runs the frame end (SR_DETCLOCK) */
void hle_vi_wait_retrace(Cpu* c) {
    if (g_present_now && g_present_now == ld32(OS_CURRENT_THREAD)) return;
    { int ppc_detclock(void); void vi_det_frame_wait(uint32_t thread);
      if (ppc_detclock() && ld32(OS_CURRENT_THREAD) == g_main_thread) {
          vi_det_frame_wait(g_main_thread);
          f_80017038_orig(c);
          void vi_det_frame_done(void);
          vi_det_frame_done();
          return;
      } }
    f_80017038_orig(c);
}

static void stats(int drawn, int late) {
    g_n_iter++;
    g_n_drawn += drawn;
    g_n_late += late;
    uint32_t f = vi_field_count();
    if (!g_stat_field) g_stat_field = f;
    if (f - g_stat_field >= 600) {
        double s = (f - g_stat_field) / 59.94;
        rt_log("[pace] %s: %.1f steps/s, %.1f drawn frames/s, %u late presents, %u schedule restarts; "
               "game work %.1f ms per drawn frame, GP thread %.1f ms, GPU %.1f ms per frame",
               settings_framerate_name(g_settings.framerate), g_n_iter / s, g_n_drawn / s, g_n_late, g_n_rebase, g_busy_ms,
               g_gp_ms, platform_gpu_frame_ms());
        if (g_settings.framerate == FR_60)
            rt_log("[pace]   60 fps: at a steady 30 %.0f%% of the time, %u switches to 30, next try at 60 after %.0f s",
                   g_n_iter ? 100.0 * g_n_s30_iter / g_n_iter : 0.0, g_n_s30_switch, g_s30_hold / 59.94);
        g_n_iter = g_n_drawn = g_n_late = g_n_rebase = g_n_s30_iter = g_n_s30_switch = 0;
        g_stat_field = f;
    }
}

/* SR_TEST_GAME_LOAD_MS=DRAW[:LOGIC[:FIELD]] (testing): busy-wait DRAW ms in every drawn iteration and
 * LOGIC ms in every iteration, on the game thread before the frame end, to imitate a slower console on
 * PC; with FIELD only until that VI field (a heavy scene that ends) */
static void test_load(int drawn) {
    static int init;
    static double draw_ms, logic_ms;
    static uint32_t until;
    if (!init++) {
        const char* e = getenv("SR_TEST_GAME_LOAD_MS");
        if (e) {
            draw_ms = atof(e);
            const char* k = strchr(e, ':');
            if (k) logic_ms = atof(k + 1);
            if (k && (k = strchr(k + 1, ':'))) until = (uint32_t)atoi(k + 1);
            rt_log("[pace] test load: %.1f ms per drawn iteration, %.1f ms per iteration, until field %u", draw_ms,
                   logic_ms, until);
        }
    }
    if (until && vi_field_count() >= until) return;
    double ms = logic_ms + (drawn ? draw_ms : 0.0);
    if (ms <= 0.0) return;
    uint64_t end = platform_now_ns() + (uint64_t)(ms * 1e6);
    while (platform_now_ns() < end) {}
}

static void hle_frame_end_mode(Cpu* c, int mode, int skipped);

static void auto_restart_schedule(void) {
    g_base = vi_field_count();
    g_steps = 0;
    g_skips_in_row = 0;
}

/* SR_RAMHASH=<steps> (determinism testing, online play groundwork): at each of the first <steps> frame
 * ends, a checksum of each 1 MB block of guest RAM, logged as "[hash] <step> <block hashes>". Two
 * runs with the same inputs (or a PC and a Switch run) must print the same lines; the first line and
 * block that differ show where they diverged. */
#ifdef __SWITCH__
#define DUMP_DIR "sdmc:/switch/sonicriders/"  /* RAM dumps next to the log */
#else
#define DUMP_DIR "build/"
#endif
/* SR_RAMHASH_REF=<file>: another machine's SR_RAMHASH log (same settings and inputs). Each step's
 * checksums are compared with it; where a block first disagrees after agreeing (up to
 * SR_RAMHASH_REF_DUMPS times, default 3) all of RAM goes to ramdiff_<step>.bin, to be compared with
 * the other machine's SR_RAMDUMPAT dump at that step. That catches a difference that comes and goes
 * at a different place each run. */
static void ram_hash_ref(unsigned step, const uint32_t* hv, uint32_t blk_mask) {
    static uint32_t (*ref)[28];
    static uint32_t* have;  /* bit per step: the reference has it */
    static unsigned nref;
    static int init, dumps_left;
    static uint32_t prev_bad;
    if (!init) {
        init = 1;
        const char* path = getenv("SR_RAMHASH_REF");
        FILE* f = path ? fopen(path, "r") : NULL;
        if (!f) { if (path) rt_log("[hash] SR_RAMHASH_REF: cannot open %s", path); return; }
        nref = 20000;
        ref = calloc(nref, sizeof *ref);
        have = calloc((nref + 31) / 32, 4);
        dumps_left = getenv("SR_RAMHASH_REF_DUMPS") ? atoi(getenv("SR_RAMHASH_REF_DUMPS")) : 3;
        static char l[512];
        unsigned got = 0;
        while (fgets(l, sizeof l, f)) {
            char* q = strstr(l, "[hash] ");
            if (!q) continue;
            char* e;
            unsigned st = (unsigned)strtoul(q + 7, &e, 10);
            if (st >= nref || e == q + 7) continue;
            if (!strncmp(e, " key=", 5)) strtoul(e + 5, &e, 10);
            for (int b = 0; b < 28; b++) {
                while (*e == ' ') e++;
                if (*e == '-') { ref[st][b] = 0; e++; continue; }
                ref[st][b] = (uint32_t)strtoul(e, &e, 16);
            }
            have[st / 32] |= 1u << (st % 32);
            got++;
        }
        fclose(f);
        rt_log("[hash] SR_RAMHASH_REF: %u reference steps from %s", got, path);
    }
    if (!ref || step >= nref || !(have[step / 32] >> (step % 32) & 1)) return;
    uint32_t bad = 0;
    for (int b = 0; b < 28; b++)
        if (blk_mask >> b & 1 && ref[step][b] && ref[step][b] != hv[b]) bad |= 1u << b;
    if (bad & ~prev_bad) {
        rt_log("[hash] step %u differs from the reference in blocks %08X", step, bad);
        if (dumps_left > 0) {
            dumps_left--;
            char path[96];
            snprintf(path, sizeof path, DUMP_DIR "ramdiff_%u.bin", step);
            FILE* f = fopen(path, "wb");
            if (f) { fwrite(mem_phys(0), 1, GUEST_RAM_SIZE, f); fclose(f); }
        }
    }
    prev_bad = bad;
}

/* the checksum of 1 MB block `blk` of guest RAM (SR_RAMHASH; netplay.c's per-frame state checksum) */
uint32_t ram_block_hash(uint32_t blk) {
    const uint64_t* p = (const uint64_t*)(mem_phys(0) + ((size_t)blk << 20));
    uint64_t h0 = 0x9E3779B97F4A7C15ull, h1 = 0, h2 = 0, h3 = 0;  /* cheap: 4 lanes, add/xor/rotate */
    for (size_t i = 0; i < (1u << 20) / 8; i += 4) {
        h0 = ((h0 << 5) | (h0 >> 59)) ^ p[i];
        h1 = ((h1 << 7) | (h1 >> 57)) + p[i + 1];
        h2 = ((h2 << 11) | (h2 >> 53)) ^ p[i + 2];
        h3 = ((h3 << 13) | (h3 >> 51)) + p[i + 3];
    }
    uint64_t h = (h0 ^ (h1 * 3) ^ (h2 * 5) ^ (h3 * 7)) * 0x100000001B3ull;
    return (uint32_t)(h ^ (h >> 32));
}

static void ram_hash(void) {
    static int limit = -1;
    static unsigned step;
    if (limit < 0) limit = getenv("SR_RAMHASH") ? atoi(getenv("SR_RAMHASH")) : 0;
    if (!limit || step >= (unsigned)limit) return;
    {   /* SR_RAMDUMPAT=<step>[,<step>...]: all of guest RAM at that step to build/ramstep.bin (the first
         * step) or build/ramstep_<step>.bin (the others), to diff two runs or find variables */
        const char* d = getenv("SR_RAMDUMPAT");
        for (const char* q = d; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL) {
            if ((unsigned)atoi(q) != step) continue;
            char path[64];
            if (q == d) snprintf(path, sizeof path, DUMP_DIR "ramstep.bin");
            else snprintf(path, sizeof path, DUMP_DIR "ramstep_%u.bin", step);
            FILE* f = fopen(path, "wb");
            if (f) { fwrite(mem_phys(0), 1, GUEST_RAM_SIZE, f); fclose(f); }
        }
    }
    char line[64 + 28 * 9];
    /* SR_RAMHASH_KEY=<address>: also log that u32 (e.g. 8021BB64, frames since the race scene started),
     * to line up two runs whose loading took a different number of frames */
    static uint32_t key_addr = 1;
    if (key_addr == 1) key_addr = getenv("SR_RAMHASH_KEY") ? (uint32_t)strtoul(getenv("SR_RAMHASH_KEY"), NULL, 16) : 0;
    {   /* SR_RAMDUMPKEY=<value>[,<value>...]: RAM to build/ramkey.bin (first value) or
         * build/ramkey_<value>.bin (the others) the first time the key reaches that value */
        static uint32_t done;  /* bit per list entry */
        const char* dk = getenv("SR_RAMDUMPKEY");
        const char* after = getenv("SR_RAMDUMPKEY_AFTER");  /* not before this frame (a later scene) */
        int idx = 0;
        if (key_addr && (!after || step >= (unsigned)atoi(after)))
            for (const char* q = dk; q && *q && idx < 32; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL, idx++) {
                if (done >> idx & 1 || ld32(key_addr) != (uint32_t)atoi(q)) continue;
                done |= 1u << idx;
                char path[64];
                if (q == dk) snprintf(path, sizeof path, DUMP_DIR "ramkey.bin");
                else snprintf(path, sizeof path, DUMP_DIR "ramkey_%d.bin", atoi(q));
                FILE* f = fopen(path, "wb");
                if (f) { fwrite(mem_phys(0), 1, GUEST_RAM_SIZE, f); fclose(f); }
            }
    }
    int n = key_addr ? snprintf(line, 64, "[hash] %u key=%u", step++, ld32(key_addr)) : snprintf(line, 64, "[hash] %u", step++);
    /* SR_RAMHASH_BLOCKS=<from>-<to>[,...] (MB): hash only those blocks (others print "-"), so the
     * measurement stays cheap enough not to slow frames down; "none" logs only the step and key */
    static uint32_t blk_mask = 0;
    static int blk_init;
    if (!blk_init) {
        blk_init = 1;
        const char* bl = getenv("SR_RAMHASH_BLOCKS");
        if (!bl) blk_mask = 0xFFFFFFFFu;
        for (const char* q = bl; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL) {
            int a = atoi(q), b = a;
            const char* dash = strchr(q, '-');
            const char* comma = strchr(q, ',');
            if (dash && (!comma || dash < comma)) b = atoi(dash + 1);
            for (int i = a; i <= b && i < 32; i++) if (i >= 0 && (*q >= '0' && *q <= '9')) blk_mask |= 1u << i;
        }
    }
    const uint8_t* ram = mem_phys(0);
    uint32_t hv[32] = {0};
    for (uint32_t blk = 0; blk < GUEST_RAM_SIZE >> 20; blk++) {
        if (!(blk_mask >> blk & 1)) { n += snprintf(line + n, sizeof line - n, " -"); continue; }
        hv[blk] = ram_block_hash(blk);
        n += snprintf(line + n, sizeof line - n, " %08x", hv[blk]);
    }
    rt_log("%s", line);
    ram_hash_ref(step - 1, hv, blk_mask);
    {   /* SR_RAMHASH_SUB=<block>: also 64 checksums of 16 KB each inside that 1 MB block ("[hsub] <step>
         * ..."), to narrow down which variable two runs disagree on */
        static int sub = -2;
        if (sub == -2) sub = getenv("SR_RAMHASH_SUB") ? atoi(getenv("SR_RAMHASH_SUB")) : -1;
        if (sub >= 0 && sub < (int)(GUEST_RAM_SIZE >> 20)) {
            char sl[32 + 64 * 9];
            int m = snprintf(sl, 32, "[hsub] %u", step - 1);
            for (int k = 0; k < 64; k++) {
                const uint32_t* p = (const uint32_t*)(ram + ((size_t)sub << 20) + ((size_t)k << 14));
                uint32_t h = 2166136261u;
                for (size_t i = 0; i < (1u << 14) / 4; i++) h = (h ^ p[i]) * 16777619u;
                m += snprintf(sl + m, sizeof sl - m, " %08x", h);
            }
            rt_log("%s", sl);
        }
    }
}

/* SR_SHOTSTEP=<frame>[,<frame>...] (determinism tests): a screenshot of the frame drawn at those game
 * frames (frame ends), so two runs can be compared picture by picture even when their presented-frame
 * counts differ (the game's 30 fps mode while loading); they are saved in that order */
static void shot_step(void) {
    static const char* list = (const char*)1;
    static unsigned step;
    if (list == (const char*)1) list = getenv("SR_SHOTSTEP");
    step++;
    for (const char* q = list; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
        if ((unsigned)atoi(q) == step) {
            void overlay_request_screenshot(void);
            overlay_request_screenshot();
            rt_log("[det] screenshot requested at game frame %u", step);
        }
}

void hle_frame_end(Cpu* c) {
    ram_hash();
    { void netplay_frame_start(void); netplay_frame_start(); }  /* the state checksum for record/replay/online */
    shot_step();
    g_main_thread = ld32(OS_CURRENT_THREAD);
    { void ppc_detclock_step(void); ppc_detclock_step(); }  /* SR_DETCLOCK: game time moves one frame on */
    { void di_det_poll(void); di_det_poll(); }  /* SR_DETCLOCK: a disc command in progress completes */
    { int ppc_detclock(void); void ai_det_frame(void); void sched_det_frame(void);
      if (ppc_detclock()) { ai_det_frame(); sched_det_frame(); } }  /* this frame's audio interrupts; due alarms */
    { int ppc_detclock(void); void si_det_latch(void); if (ppc_detclock()) si_det_latch(); }  /* and the next frame's input */
    {   /* SR_DETCLOCK: no frame pacing (its skips and its 30 fps switch follow the host's timing and
         * change the game's step); the game's own frame end, as on the console, including its own
         * 30 fps mode while loading */
        int ppc_detclock(void); void vi_det_frame_end(uint32_t thread, int in);
        if (ppc_detclock()) {
            vi_det_frame_end(g_main_thread, 1);
            f_8000610C_orig(c);
            vi_det_frame_end(g_main_thread, 0);
            return;
        }
    }
    int mode = g_settings.framerate;
    int skipped = ld32(GAME_FRAME_SKIP) != 0;  /* this iteration's draws were left out */
    test_load(!skipped);
    /* the game thread's work in this iteration: since the previous frame end returned (its wait for the
     * retrace is not work) */
    uint64_t now = platform_now_ns();
    if (!skipped && g_t_exit) {
        double ms = (now - g_t_exit) / 1e6;
        if (ms < 50.0) g_busy_ms = g_busy_ms < 0 ? ms : g_busy_ms * 0.95 + ms * 0.05;
    }
    if (!skipped) {  /* the GP thread's work since the previous drawn frame */
        uint64_t gx_gp_busy_total(void);
        uint64_t total = gx_gp_busy_total();
        if (g_gp_prev) {
            double ms = (double)(total - g_gp_prev) / 40500.0;
            if (ms < 60.0) g_gp_ms = g_gp_ms < 0 ? ms : g_gp_ms * 0.95 + ms * 0.05;
        }
        g_gp_prev = total;
    }
    if (mode != g_prev_mode) {
        auto_restart_schedule();
        g_prev_mode = mode;
        g_s30 = 0;
        g_s30_hold = S30_HOLD_MIN;
        g_s30_since = g_win_field = vi_field_count();
        g_win_trouble = g_win_rebase = 0;
    }
    hle_frame_end_mode(c, mode, skipped);
    g_t_exit = platform_now_ns();
}

static void hle_frame_end_mode(Cpu* c, int mode, int skipped) {

    if (mode == FR_30) {
        st32(GAME_FRAME_MODE, 1);  /* the frame end toggles the skip flag: drawn and undrawn in turn */
        f_8000610C_orig(c);
        set_steps(2.0f);  /* scene resets set this too; a change in the middle of a race does not wait for one */
        stats(!skipped, 0);
        return;
    }

    if (mode == FR_60 && g_s30) {
        st32(GAME_FRAME_MODE, 1);  /* as FR_30 */
        f_8000610C_orig(c);
        set_steps(2.0f);
        g_n_s30_iter++;
        stats(!skipped, 0);
        uint32_t f = vi_field_count();
        double gpu = platform_gpu_frame_ms();
        int fits = g_busy_ms >= 0 && g_busy_ms < S30_FIT_MS && g_gp_ms < S30_FIT_MS && gpu < S30_FIT_MS;
        /* back to 60 after an undrawn iteration (the next one draws, covering both steps) */
        if (skipped && f - g_s30_since >= g_s30_hold && fits) {
            g_s30 = 0;
            g_s30_since = g_win_field = f;
            g_win_trouble = g_win_rebase = 0;
            st32(GAME_FRAME_MODE, 0);
            auto_restart_schedule();
        }
        return;
    }

    /* FR_AUTO, and FR_60 at 60. An undrawn iteration ends through the game's skip path (mode 1, skip
     * 1 -> 0: no present, no wait); a drawn one through the normal path (mode 0): wait for the retrace,
     * present. */
    int late = 0;
    st32(GAME_FRAME_MODE, skipped ? 1 : 0);
    if (!skipped) {
        /* this step's present was due at retrace g_base + g_steps + 1: if that has passed, present now */
        late = (int32_t)(vi_field_count() - (g_base + g_steps + 1)) >= 0;
        g_present_now = late ? ld32(OS_CURRENT_THREAD) : 0;
    }
    f_8000610C_orig(c);
    g_present_now = 0;
    g_steps++;

    int32_t lag = (int32_t)(vi_field_count() - (g_base + g_steps));
    if (lag > FR_REBASE || lag < -2) {
        g_base = vi_field_count() - g_steps;
        lag = 0;
        g_n_rebase++;
        g_win_rebase = 1;  /* a load or a hitch: not a reason to switch to 30 */
    }
    if (mode == FR_60) {  /* keeps falling behind: the game's steady 30 fps mode from the next iteration */
        g_win_trouble += late + (lag >= 1);
        uint32_t f = vi_field_count();
        if (f - g_win_field >= 60) {
            if (g_win_trouble >= S30_TROUBLE && !g_win_rebase) {
                if (f - g_s30_since < S30_SHORT && g_n_s30_total)
                    g_s30_hold = g_s30_hold * 2 > S30_HOLD_MAX ? S30_HOLD_MAX : g_s30_hold * 2;
                else if (f - g_s30_since >= 2 * S30_SHORT)
                    g_s30_hold = S30_HOLD_MIN;
                g_s30 = 1;
                g_s30_since = f;
                g_n_s30_switch++;
                g_n_s30_total++;
            }
            g_win_field = f;
            g_win_trouble = g_win_rebase = 0;
        }
    }
    if (lag >= 1 && g_skips_in_row < FR_MAX_SKIPS) {  /* behind: the next iteration only runs the game */
        g_skips_in_row++;
        st32(GAME_FRAME_MODE, 1);
        st32(GAME_FRAME_SKIP, 1);
        set_steps(2.0f);  /* as the game's own mode during an undrawn iteration */
    } else {
        st32(GAME_FRAME_MODE, 0);
        st32(GAME_FRAME_SKIP, 0);
        set_steps((float)(1 + g_skips_in_row));  /* the drawn frame covers the skipped steps too */
        g_skips_in_row = 0;
    }
    stats(!skipped, late);
}
