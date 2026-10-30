/* SR_PERFTEST=1: a fixed, repeatable performance run (tools/perf_test.py, docs/PERF.md), on PC or on the
 * Switch (env.txt). It needs no save and no input: the game starts on a fresh memory card kept in memory
 * (the player's save is not read or written), controllers are ignored, the intro movie is skipped, and the
 * title screen's attract mode then plays two demo races by itself: Metal City (1 player) and a 2-player
 * split-screen race. In each, 30 s of race (scene frames 240-2039) are measured on the host clock:
 *
 *  - speed: scene frames per second (the game's own frame counter; 60 = full game speed, less is slow motion)
 *  - steps/s: game loop iterations (frame ends) per second (30 in the game's own 30 fps mode at full speed)
 *  - fps: pictures presented per second
 *  - game ms: the game thread's work per step (from the end of one frame end to the next, so without the
 *    wait for the retrace): mean, median, 95th percentile, longest; and how many steps took over 16.7 ms
 *
 * Results go to the log ("[perftest] ...") and to perftest.json (PC: build/, or SR_PERFTEST_OUT; Switch: next
 * to the log). The PC build then exits; the Switch shows a message (closing from HOME is fine).
 * Normal frame pacing stays on (the player's frame rate setting), as when playing; SR_DETCLOCK is not used. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overlay.h"
#include "runtime.h"
#include "settings.h"

#define SCENE_KEY 0x8021BB64u  /* frames since the current scene started (the game's own counter) */
#define WIN_FROM 240u
#define WIN_LEN 1800u
#define NSCENES 2

#ifdef __SWITCH__
#define OUT_DEFAULT "sdmc:/switch/sonicriders/perftest.json"
#define PLATFORM "switch"
#else
#define OUT_DEFAULT "build/perftest.json"
#define PLATFORM "pc"
#endif

extern const char g_build_version[];
unsigned long long gx_frames_presented(void);

static const char* const k_names[NSCENES] = {"demo_1p", "demo_2p"};

typedef struct {
    uint64_t t0, t1, f0, f1;  /* host ns and presented frames at the window's ends */
    uint32_t k0, k1;          /* scene frames at the window's ends */
    float work[4096];
    unsigned n;
    int done;
} Scene;

static int g_on = -1;
static Scene g_sc[NSCENES];
static int g_long_scenes;        /* scenes that lasted 100+ frames so far (1 = boot and title) */
static uint32_t g_prev_key;
static uint64_t g_end_ns;        /* when the last frame end returned */
static int g_finished;

int perftest_on(void) {
    if (g_on < 0) { const char* e = getenv("SR_PERFTEST"); g_on = e && atoi(e) != 0; }
    return g_on;
}

/* si.c: what the game sees on a controller during the run (the intro movie skipped with START) */
void perftest_pad(GcPad* p) {
    static const GcPad neutral = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    *p = neutral;
    uint32_t f = vi_field_count();
    if ((f >= 1500 && f < 1510)) p->buttons = 0x1000;  /* START: skip the intro movie */
}

static int cmp_float(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

static void finish(void) {
    g_finished = 1;
    const char* path = getenv("SR_PERFTEST_OUT") ? getenv("SR_PERFTEST_OUT") : OUT_DEFAULT;
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, "{\n  \"version\": 1,\n  \"build\": \"%s\",\n  \"platform\": \"%s\",\n", g_build_version, PLATFORM);
        fprintf(f, "  \"settings\": {\"framerate\": \"%s\", \"resolution\": \"%s\"},\n  \"scenes\": {\n",
                settings_framerate_name(g_settings.framerate), settings_resolution_name(g_settings.resolution));
    }
    for (int i = 0; i < NSCENES; i++) {
        Scene* s = &g_sc[i];
        double secs = (double)(s->t1 - s->t0) / 1e9;
        static float sorted[4096];
        memcpy(sorted, s->work, s->n * sizeof(float));
        qsort(sorted, s->n, sizeof(float), cmp_float);
        double sum = 0;
        unsigned slow = 0;
        for (unsigned k = 0; k < s->n; k++) { sum += sorted[k]; slow += sorted[k] > 16.7f; }
        double speed = secs > 0 ? (double)(s->k1 - s->k0) / secs : 0;
        double steps_s = secs > 0 ? s->n / secs : 0, fps = secs > 0 ? (double)(s->f1 - s->f0) / secs : 0;
        double mean = s->n ? sum / s->n : 0;
        double p50 = s->n ? sorted[s->n / 2] : 0, p95 = s->n ? sorted[s->n * 95 / 100] : 0, mx = s->n ? sorted[s->n - 1] : 0;
        rt_log("[perftest] %s: speed %.2f, %.2f steps/s, %.2f fps; game ms per step: mean %.2f, median %.2f, p95 %.2f, max %.2f; "
               "%u of %u steps over 16.7 ms", k_names[i], speed, steps_s, fps, mean, p50, p95, mx, slow, s->n);
        if (f)
            fprintf(f, "    \"%s\": {\"speed\": %.3f, \"steps_per_s\": %.3f, \"fps\": %.3f, \"game_ms_mean\": %.3f, \"game_ms_p50\": %.3f, "
                       "\"game_ms_p95\": %.3f, \"game_ms_max\": %.3f, \"steps_over_16ms\": %u, \"steps\": %u}%s\n",
                    k_names[i], speed, steps_s, fps, mean, p50, p95, mx, slow, s->n, i + 1 < NSCENES ? "," : "");
    }
    if (f) {
        fprintf(f, "  }\n}\n");
        fclose(f);
        rt_log("[perftest] results written to %s", path);
    } else {
        rt_log("[perftest] cannot write %s", path);
    }
#ifdef __SWITCH__
    overlay_set_banner("Performance test finished: results in perftest.json.\nClose the game from HOME.");
#else
    rt_log("[perftest] done");
    exit(0);
#endif
}

/* hle_frame_end, on entry: one game step ends here */
void perftest_frame_begin(void) {
    if (!perftest_on() || g_finished) return;
    uint64_t now = platform_now_ns();
    uint32_t key = ld32(SCENE_KEY);
    if (key < g_prev_key && g_prev_key >= 100) {
        g_long_scenes++;
        rt_log("[perftest] scene %d starts at presented frame %llu", g_long_scenes + 1, gx_frames_presented());
    }
    g_prev_key = key;
    int idx = g_long_scenes - 1;  /* long scene 2 (index 0 after the boot scene) = demo 1, then title, demo 2 */
    idx = idx == 0 ? 0 : idx == 2 ? 1 : -1;
    if (idx >= 0 && !g_sc[idx].done) {
        Scene* s = &g_sc[idx];
        if (!s->t0 && key >= WIN_FROM && key < WIN_FROM + 60) { s->t0 = now; s->f0 = gx_frames_presented(); s->k0 = key; }
        else if (s->t0 && key > s->k0) {
            if (s->n < 4096) s->work[s->n++] = g_end_ns ? (float)((double)(now - g_end_ns) / 1e6) : 0;
            if (key >= s->k0 + WIN_LEN) {
                s->k1 = key;
                s->t1 = now;
                s->f1 = gx_frames_presented();
                s->done = 1;
                rt_log("[perftest] %s measured", k_names[idx]);
                if (idx == NSCENES - 1) finish();
            }
        }
    }
}

/* hle_frame_end, on return: the game's work for the next step starts */
void perftest_frame_done(void) {
    if (g_on > 0) g_end_ns = platform_now_ns();
}
