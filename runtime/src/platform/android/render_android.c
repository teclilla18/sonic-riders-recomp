/* Android backend, milestone 2 (docs/ANDROID_PLAN.md): no picture yet. Frames built by gx_render.c are
 * recycled as soon as they arrive (as the desktop's SR_HEADLESS), while a render thread keeps SDL's events
 * and the controllers going, so the game, its sound and input run. Milestone 3 replaces this with the
 * Vulkan renderer (ported from platform/switch/render_deko.c). */
#include <SDL.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "gx_render.h"
#include "overlay.h"
#include "runtime.h"

void input_init(void);
void input_pump(void);

static mtx_t g_lock;
static cnd_t g_cv;
static GxFrame g_frames[2];
static GxFrame* g_free[2];
static int g_nfree;
static GxFrame* g_pending;
static volatile int g_quit;
static unsigned g_presented;

void render_dump(void) {}

GxFrame* render_take_free_frame(void) {
    mtx_lock(&g_lock);
    while (!g_nfree) cnd_wait(&g_cv, &g_lock);
    GxFrame* f = g_free[--g_nfree];
    mtx_unlock(&g_lock);
    return f;
}

void render_submit(GxFrame* f) {
    mtx_lock(&g_lock);
    while (g_pending) cnd_wait(&g_cv, &g_lock);
    g_pending = f;
    cnd_broadcast(&g_cv);
    mtx_unlock(&g_lock);
}

int render_poll_quit(void) { return g_quit; }

static void pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: g_quit = 1; break;
        case SDL_APP_WILLENTERBACKGROUND: rt_log("[android] going to the background"); break;
        case SDL_APP_DIDENTERFOREGROUND: rt_log("[android] back in the foreground"); break;
        case SDL_APP_LOWMEMORY: rt_log("[android] the system is low on memory"); break;
        default: break;
        }
    }
    input_pump();
}

static int render_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);
    uint64_t t0 = platform_now_ns();
    for (;;) {
        mtx_lock(&g_lock);
        while (!g_pending) {
            mtx_unlock(&g_lock);
            pump();
            mtx_lock(&g_lock);
            if (!g_pending) {
                struct timespec ts;
                timespec_get(&ts, TIME_UTC);
                ts.tv_nsec += 4000000;
                if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
                cnd_timedwait(&g_cv, &g_lock, &ts);
            }
        }
        GxFrame* f = g_pending;
        mtx_unlock(&g_lock);
        pump();
        uint32_t draws = 0;
        for (uint32_t i = 0; i < f->ncmds; i++) draws += f->cmds[i].type == GXC_DRAW;
        for (uint32_t i = 0; i < f->nretired; i++) gx_texture_free(f->retired[i]);
        g_presented++;
        if (g_presented % 300 == 0) {  /* progress without a picture: frames, draws, rate */
            double s = (double)(platform_now_ns() - t0) / 1e9;
            rt_log("[android] frame %u, %u draws, %.1f frames/s since start", g_presented, draws, g_presented / s);
        }
        mtx_lock(&g_lock);
        g_pending = NULL;
        g_free[g_nfree++] = f;
        cnd_broadcast(&g_cv);
        mtx_unlock(&g_lock);
        if (overlay_take_quit_request()) g_quit = 1;
        if (g_quit) {
            rt_log("[android] quit");
            fflush(stderr);
            exit(0);
        }
    }
    return 0;
}

void render_start(void) {
    mtx_init(&g_lock, mtx_plain);
    cnd_init(&g_cv);
    g_free[0] = &g_frames[0];
    g_free[1] = &g_frames[1];
    g_nfree = 2;
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) rt_fatal("SDL_Init: %s", SDL_GetError());
    input_init();
    rt_thread_start(render_thread, NULL);
}
