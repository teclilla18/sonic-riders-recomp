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

void f_8000610C_orig(Cpu* c);  /* the frame end (generated) */
void f_80017038_orig(Cpu* c);  /* VIWaitForRetrace */

static uint32_t g_present_now; /* guest thread whose frame end skips VIWaitForRetrace (0: none). Other
                                * threads (CRI, sound) wait for retraces in loops: they must never skip */
static uint32_t g_base;        /* Auto: retrace count at which step 0 was due */
static uint32_t g_steps;       /* Auto: iterations since g_base */
static int g_skips_in_row;
static int g_prev_mode = -1;
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
        rt_log("[pace] %s: %.1f steps/s, %.1f drawn frames/s, %u late presents, %u schedule restarts",
               settings_framerate_name(g_settings.framerate), g_n_iter / s, g_n_drawn / s, g_n_late, g_n_rebase);
        g_n_iter = g_n_drawn = g_n_late = g_n_rebase = 0;
        g_stat_field = f;
    }
}

/* SR_TEST_GAME_LOAD_MS=DRAW[:LOGIC] (testing): busy-wait DRAW ms in every drawn iteration and LOGIC ms
 * in every iteration, on the game thread before the frame end, to imitate a slower console on PC */
static void test_load(int drawn) {
    static int init;
    static double draw_ms, logic_ms;
    if (!init++) {
        const char* e = getenv("SR_TEST_GAME_LOAD_MS");
        if (e) {
            draw_ms = atof(e);
            const char* k = strchr(e, ':');
            if (k) logic_ms = atof(k + 1);
            rt_log("[pace] test load: %.1f ms per drawn iteration, %.1f ms per iteration", draw_ms, logic_ms);
        }
    }
    double ms = logic_ms + (drawn ? draw_ms : 0.0);
    if (ms <= 0.0) return;
    uint64_t end = platform_now_ns() + (uint64_t)(ms * 1e6);
    while (platform_now_ns() < end) {}
}

void hle_frame_end(Cpu* c) {
    int mode = g_settings.framerate;
    int skipped = ld32(GAME_FRAME_SKIP) != 0;  /* this iteration's draws were left out */
    test_load(!skipped);
    if (mode != g_prev_mode) {
        g_base = vi_field_count();
        g_steps = 0;
        g_skips_in_row = 0;
        g_prev_mode = mode;
    }

    if (mode == FR_30) {
        st32(GAME_FRAME_MODE, 1);  /* the frame end toggles the skip flag: drawn and undrawn in turn */
        f_8000610C_orig(c);
        stats(!skipped, 0);
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
