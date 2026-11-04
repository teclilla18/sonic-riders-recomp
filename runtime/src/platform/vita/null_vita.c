/* PS Vita probe V1a: no picture, sound or controller. Frames are taken over and recycled at once, as the
 * PC's SR_HEADLESS mode does (render_gl.c), so the game thread's own work can be measured (SR_PERFTEST);
 * the controller is port 0 with nothing pressed, plus SR_INPUT scripts (input_script.c). */
#include <stdatomic.h>
#include <threads.h>

#include "gx_render.h"
#include "runtime.h"

static mtx_t g_lock;
static cnd_t g_cv;
static GxFrame g_frames[2];
static GxFrame* g_free[2];
static int g_nfree;
static GxFrame* g_pending;
static volatile int g_rphase;  /* stall dumps: 0 waiting for a frame, 1 freeing its textures, 2 handing it back */

static int render_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);
    for (;;) {
        mtx_lock(&g_lock);
        while (!g_pending) cnd_wait(&g_cv, &g_lock);
        GxFrame* f = g_pending;
        mtx_unlock(&g_lock);
        g_rphase = 1;
        for (uint32_t i = 0; i < f->nretired; i++) gx_texture_free(f->retired[i]);
        g_rphase = 2;
        mtx_lock(&g_lock);
        g_pending = NULL;
        g_free[g_nfree++] = f;
        cnd_broadcast(&g_cv);
        mtx_unlock(&g_lock);
        g_rphase = 0;
    }
    return 0;
}

void render_start(void) {
    mtx_init(&g_lock, mtx_plain);
    cnd_init(&g_cv);
    g_free[0] = &g_frames[0];
    g_free[1] = &g_frames[1];
    g_nfree = 2;
    rt_log("[vita] no renderer in this probe: frames are recycled at once");
    rt_thread_start(render_thread, NULL);
}

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

int render_poll_quit(void) { return 0; }
void gx_gp_dump(void);
void gx_decode_dump(void);
void render_dump(void) {
    gx_gp_dump();
    gx_decode_dump();
    rt_log("[vita] null renderer: phase %d, free frames %d, frame pending %d", g_rphase, g_nfree, g_pending != NULL);
}  /* stall dumps: where the GP thread is */
double platform_gpu_frame_ms(void) { return -1.0; }

void platform_audio_push(const int16_t* lr, unsigned frames) { (void)lr; (void)frames; }

void input_init(void) {}
void input_pump(void) {}
int input_connected(int chan) { return chan == 0; }
void input_get_pad(int chan, GcPad* out) {
    out->buttons = 0;
    out->stick_x = out->stick_y = out->c_x = out->c_y = 0x80;
    out->trig_l = out->trig_r = 0;
    input_script_apply(chan, out);
}
void input_set_rumble(int chan, int motor) { (void)chan; (void)motor; }
