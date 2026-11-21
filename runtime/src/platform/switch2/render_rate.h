/* Native Vulkan adaptation of host/render_gl.c's in-between-picture scheduler.
 * Included after the backend's frame queue, execute() and pump(). All held-frame
 * and GPU retirement operations belong to this render thread. */
#pragma once

#define IP_GAME_NS 16683333ull /* existing guest retrace rate: 59.94 Hz */
static GxFrame *g_ip_fa, *g_ip_fb;
static uint64_t g_ip_tb, g_ip_next;
static int g_ip_b_shown;

static int ip_target(void) {
    return g_settings.framerate == FR_30 ? 0 : g_settings.pc_fps;
}

static void give_back(GxFrame* f) {
    /* Append once, on release, behind the last submitted GPU work. Repeated
     * draws never copy retirement lists, and a never-drawn frame still retires.
     * The last submission covers all earlier uses on this single Vulkan queue. */
    unsigned slot = g_slot ^ 1;
    uint32_t n = f->nretired;
    if (n) {
        size_t total = (size_t)g_nretired[slot] + n;
        int* ids = realloc(g_retired[slot], total * sizeof(int));
        if (!ids) rt_fatal("[vk2] texture retirement allocation failed");
        g_retired[slot] = ids;
        memcpy(ids + g_nretired[slot], f->retired, n * sizeof(int));
        g_nretired[slot] += n;
        f->nretired = 0;
    }
    mtx_lock(&g_lock);
    if (g_nfree >= (int)(sizeof g_free / sizeof g_free[0])) rt_fatal("[vk2] duplicate frame release");
    g_free[g_nfree++] = f;
    cnd_broadcast(&g_cv);
    mtx_unlock(&g_lock);
}

static void ip_release(void) {
    if (g_ip_fa) give_back(g_ip_fa);
    if (g_ip_fb) give_back(g_ip_fb);
    g_ip_fa = g_ip_fb = NULL;
    g_ip_a = NULL;
    g_ip_on = 0;
}

/* Consume at game cadence even when the rendering limit is below 60. The four
 * CPU frames cover A, B, one queued picture and the producer's working picture.
 * Waits are bounded so input and live settings keep working without new draws. */
static void ip_round(int target) {
    static int previous = -999;
    static uint64_t fps_t0, log_t0;
    static unsigned fps_presented, fps_rendered;
    if (target != previous) {
        if (!target) ip_release();
        g_ip_next = 0;
        native_display_rate(target);
        g_swap_bad = 1;
        previous = target;
        rt_log("[vk2-rate] render limit=%d (0=guest, -1=unlimited); guest timing unchanged; presentation is separate", target);
    }
    g_render_counts_fps = target != 0;
    uint64_t now = platform_now_ns();
    uint64_t period = target > 0 ? 1000000000ull / (unsigned)target : 0;
    mtx_lock(&g_lock);
    if (!g_pending) {
        uint64_t wait = 4000000;
        if (target && g_ip_fb && !g_ip_b_shown)
            wait = g_ip_next > now ? g_ip_next - now : 0;
        if (wait > 4000000) wait = 4000000;
        if (wait) {
            struct timespec ts;
            timespec_get(&ts, TIME_UTC);
            uint64_t ns = (uint64_t)ts.tv_nsec + wait;
            ts.tv_sec += (time_t)(ns / 1000000000ull);
            ts.tv_nsec = (long)(ns % 1000000000ull);
            cnd_timedwait(&g_cv, &g_lock, &ts);
        }
    }
    GxFrame* nf = g_pending;
    if (nf) { g_pending = NULL; cnd_broadcast(&g_cv); }
    mtx_unlock(&g_lock);
    pump();
    if (g_quit) { if (nf) give_back(nf); return; }
    now = platform_now_ns();
    if (!target) {
        if (nf) { execute(nf); give_back(nf); }
        return;
    }
    if (nf) {
        if (g_ip_fa) give_back(g_ip_fa);
        g_ip_fa = g_ip_fb;
        g_ip_fb = nf;
        uint64_t expect = g_ip_tb + IP_GAME_NS;
        g_ip_tb = g_ip_fa && now + 4000000 > expect && now < expect + 4000000 ? expect : now;
        ip_frame_info(nf, &g_ip_info[nf - g_frames]);
        if (g_ip_fa) ip_match(g_ip_fa, &g_ip_info[g_ip_fa - g_frames], nf, &g_ip_info[nf - g_frames]);
        g_ip_b_shown = 0;
    }
    if (!g_ip_fb || g_ip_b_shown) return;
    if (period && now < g_ip_next) {
        uint64_t wait = g_ip_next - now;
        platform_sleep_ns(wait < 1000000 ? wait : 1000000);
        return;
    }
    double t = now > g_ip_tb ? (double)(now - g_ip_tb) / IP_GAME_NS : 0.0;
    g_ip_a = g_ip_fa;
    g_ip_t = (float)t;
    g_ip_on = g_ip_fa && t < 1.0;
    execute(g_ip_fb);
    g_ip_on = 0;
    if (!g_ip_fa || t >= 1.0) g_ip_b_shown = 1;
    g_ip_next = period ? now + period : 0; /* Unlimited has no software wait */
    if (!fps_t0) { fps_t0 = now; fps_presented = g_presented; fps_rendered = g_frames_done; }
    if (now - fps_t0 >= 500000000ull) {
        float renders = (float)((g_frames_done - fps_rendered) * 1e9 / (double)(now - fps_t0));
        float presents = (float)((g_presented - fps_presented) * 1e9 / (double)(now - fps_t0));
        overlay_set_fps(presents);
        if (now - log_t0 >= 10000000000ull) {
            rt_log("[vk2-rate] limit=%d render=%.1f/s present=%.1f/s mode=%d paired=%u/%u words=%u projections=%u held=%d",
                   target, renders, presents, (int)g_present_mode, g_ip_matched, g_ip_draws,
                   g_ip_npairs / 2, g_ip_nupairs / 2, !!g_ip_fa + !!g_ip_fb);
            log_t0 = now;
        }
        fps_t0 = now; fps_presented = g_presented; fps_rendered = g_frames_done;
    }
}
