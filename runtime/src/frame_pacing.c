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
 *   FR_60   the original: one drawn frame per step, slow motion when a frame is late.
 *   FR_30   the game's own 30 fps mode as it is: every second iteration skips drawing.
 *   FR_AUTO "keep game speed": draws every step while the game keeps up; when it falls behind the
 *           retrace schedule, the next iteration skips drawing (at most FR_MAX_SKIPS in a row), and
 *           a frame that finishes after its retrace is presented at once instead of waiting for the
 *           next retrace (VIWaitForRetrace skipped), as WWHD's "flip when ready" does.
 *           When that keeps happening (AUTO_TROUBLE times in a second) Auto settles on the game's
 *           steady 30 fps mode instead of jumping between 40 and 60, and tries 60 again only after a
 *           hold (doubled after each try that failed soon) and once the measured work fits a retrace:
 *           the game thread's work for a drawn iteration and the GPU's time per frame (render_deko.c,
 *           whose dynamic resolution aims at 60 fps in Auto) both under AUTO_FIT_MS.
 */
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
#define AUTO_TROUBLE 4     /* late presents + undrawn iterations within 60 retraces that make Auto settle on 30 */
#define AUTO_HOLD_MIN 300  /* retraces at 30 before 60 is tried again (5 s) */
#define AUTO_HOLD_MAX 2400 /* (40 s): the hold doubles each time 60 failed within AUTO_SHORT */
#define AUTO_SHORT 900     /* 15 s */
#define AUTO_FIT_MS 13.0   /* work per frame that leaves room in a 16.7 ms retrace */

void f_8000610C_orig(Cpu* c);  /* the frame end (generated) */
void f_80017038_orig(Cpu* c);  /* VIWaitForRetrace */

static uint32_t g_present_now; /* guest thread whose frame end skips VIWaitForRetrace (0: none). Other
                                * threads (CRI, sound) wait for retraces in loops: they must never skip */
static uint32_t g_base;        /* Auto: retrace count at which step 0 was due */
static uint32_t g_steps;       /* Auto: iterations since g_base */
static int g_skips_in_row;
static int g_prev_mode = -1;
/* Auto's steady 30 fps state */
static int g_auto30;
static uint32_t g_auto_since;              /* retrace count when Auto entered its current state */
static uint32_t g_hold = AUTO_HOLD_MIN;
static uint32_t g_win_field;               /* the trouble window */
static int g_win_trouble, g_win_rebase;
static uint64_t g_t_exit;                  /* when the previous frame end returned */
static double g_busy_ms = -1.0;            /* game-thread work of a drawn iteration, smoothed */
static unsigned g_n_iter30, g_n_fallback, g_n_fallback_total;
/* statistics for the [pace] log line */
static unsigned g_n_iter, g_n_drawn, g_n_late, g_n_rebase;
static uint32_t g_stat_field;

static void set_steps(float n) {
    if (ld32(GAME_FRAME_ADV) == 0) return;  /* as the game's frame end: no update while paused */
    float scale = u32_as_f32(ld32(GAME_SPEED_SCALE));
    if (!(scale > 0.0f && scale < 8.0f)) scale = 1.0f;
    st32(GAME_FRAME_STEPS, f32_as_u32(n * scale));
}

void hle_vi_wait_retrace(Cpu* c) {
    if (g_present_now && g_present_now == ld32(OS_CURRENT_THREAD)) return;
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
               "game work %.1f ms per drawn frame, GPU %.1f ms",
               settings_framerate_name(g_settings.framerate), g_n_iter / s, g_n_drawn / s, g_n_late, g_n_rebase, g_busy_ms,
               platform_gpu_frame_ms());
        if (g_settings.framerate == FR_AUTO)
            rt_log("[pace]   Auto: at a steady 30 fps %.0f%% of the time, %u falls back to 30, next try at 60 after %.0f s",
                   g_n_iter ? 100.0 * g_n_iter30 / g_n_iter : 0.0, g_n_fallback, g_hold / 59.94);
        g_n_iter = g_n_drawn = g_n_late = g_n_rebase = g_n_iter30 = g_n_fallback = 0;
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

void hle_frame_end(Cpu* c) {
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
    if (mode != g_prev_mode) {
        auto_restart_schedule();
        g_prev_mode = mode;
        g_auto30 = 0;
        g_hold = AUTO_HOLD_MIN;
        g_auto_since = g_win_field = vi_field_count();
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

    if (mode == FR_AUTO && g_auto30) {
        st32(GAME_FRAME_MODE, 1);  /* as FR_30 */
        f_8000610C_orig(c);
        set_steps(2.0f);
        g_n_iter30++;
        stats(!skipped, 0);
        uint32_t f = vi_field_count();
        double gpu = platform_gpu_frame_ms();
        /* back to 60 after an undrawn iteration (the next one draws, covering both steps) */
        if (skipped && f - g_auto_since >= g_hold && g_busy_ms >= 0 && g_busy_ms < AUTO_FIT_MS && gpu < AUTO_FIT_MS) {
            g_auto30 = 0;
            g_auto_since = g_win_field = f;
            g_win_trouble = g_win_rebase = 0;
            st32(GAME_FRAME_MODE, 0);
            auto_restart_schedule();
        }
        return;
    }

    if (mode == FR_60) {
        /* an undrawn iteration (left from another mode) still ends without presenting */
        st32(GAME_FRAME_MODE, skipped ? 1 : 0);
        f_8000610C_orig(c);
        if (skipped || ld32(GAME_FRAME_MODE)) {
            st32(GAME_FRAME_MODE, 0);
            st32(GAME_FRAME_SKIP, 0);
            set_steps(1.0f);
        }
        stats(!skipped, 0);
        return;
    }

    /* FR_AUTO. An undrawn iteration ends through the game's skip path (mode 1, skip 1 -> 0: no
     * present, no wait); a drawn one through the normal path (mode 0): wait for the retrace, present. */
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
        g_win_rebase = 1;  /* a load or a hitch: not a reason to settle on 30 */
    }
    /* keeps falling behind: settle on 30 (the next iteration is set up below as usual; the 30 fps mode
     * takes over from the one after) */
    g_win_trouble += late + (lag >= 1);
    {
        uint32_t f = vi_field_count();
        if (f - g_win_field >= 60) {
            if (g_win_trouble >= AUTO_TROUBLE && !g_win_rebase) {
                if (f - g_auto_since < AUTO_SHORT && g_n_fallback_total)
                    g_hold = g_hold * 2 > AUTO_HOLD_MAX ? AUTO_HOLD_MAX : g_hold * 2;
                else if (f - g_auto_since >= 2 * AUTO_SHORT)
                    g_hold = AUTO_HOLD_MIN;
                g_auto30 = 1;
                g_auto_since = f;
                g_n_fallback++;
                g_n_fallback_total++;
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
