/* Video Interface: a register model so the game's own VI library (VIInit, VIConfigure,
 * VIWaitForRetrace, retrace callbacks, __VIRetraceHandler) runs recompiled.
 * A host thread raises the retrace interrupt (DI0) at the NTSC field rate, 59.94 Hz. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "game_addrs.h"
#include "runtime.h"

#define OS_INTERRUPT_PI_VI 24

static volatile uint16_t g_vi[0x80];  /* 0xCC002000-0xCC0020FF, 16-bit registers */
static uint64_t g_field_start;        /* timebase tick of the current field's start */
static volatile uint32_t g_fields;
#define TICKS_PER_FIELD (40500000ull * 1001 / 60000)  /* 59.94 Hz */

uint32_t vi_read(uint32_t off, int size) {
    off &= 0xFF;
    if ((off & ~3u) == 0x2C) {  /* VCT (line) / HCT (pixel) of the beam */
        uint64_t t = rt_ticks() - g_field_start;
        int ppc_detclock(void); uint64_t ppc_timebase(void), ppc_detclock_base(void);
        if (ppc_detclock()) t = ppc_timebase() - ppc_detclock_base();  /* SR_DETCLOCK: from game time */
        uint32_t line = (uint32_t)(t * 263 / TICKS_PER_FIELD) + 1, pix = 1;
        if (line > 263) line = 263;
        uint32_t v = line << 16 | pix;
        return size == 4 ? v : (off & 2) ? (v & 0xFFFF) : (v >> 16);
    }
    if (size == 4) return (uint32_t)g_vi[off >> 1] << 16 | g_vi[(off >> 1) + 1];
    if (size == 2) return g_vi[off >> 1];
    return (off & 1) ? (g_vi[off >> 1] & 0xFF) : (g_vi[off >> 1] >> 8);
}

void vi_write(uint32_t off, uint32_t v, int size) {
    off &= 0xFF;
    if (size == 4) {
        g_vi[off >> 1] = (uint16_t)(v >> 16);
        g_vi[(off >> 1) + 1] = (uint16_t)v;
    } else if (size == 2) {
        g_vi[off >> 1] = (uint16_t)v;
    } else {
        uint16_t r = g_vi[off >> 1];
        g_vi[off >> 1] = (off & 1) ? (uint16_t)((r & 0xFF00) | (v & 0xFF)) : (uint16_t)((r & 0xFF) | (v << 8));
    }
}

/* the frame buffer the game asked VI to show next (top field), for presentation */
uint32_t vi_xfb_address(void) {
    uint32_t tfbl = (uint32_t)g_vi[0x1C >> 1] << 16 | g_vi[0x1E >> 1];
    uint32_t a = tfbl & 0x00FFFFFF;
    if (tfbl & 0x10000000u) a <<= 5;  /* page-offset bit: address in 32-byte units */
    return 0x80000000u | a;
}

uint32_t vi_field_count(void) { return g_fields; }

/* SR_DETCLOCK (online play groundwork): one retrace per game frame, handed over where the game's main
 * thread waits for it (VIWaitForRetrace, frame_pacing.c): vi_thread raises it once the main thread
 * sleeps in that wait (it checks every 2 ms), so the main thread never sees it arrive mid-work. Retraces that fall due while the game is still working on its frame wait (at
 * most 2 are kept) instead of landing in the middle of its work, so every run sees them at the same
 * points. Before the first game frame, and when the main thread has not waited for a retrace for 10 fields
 * (loading, or code that polls the retrace count instead of waiting), vi_thread delivers them as usual
 * (not deterministic there: online races start from a barrier after loading), so nothing waiting for one stalls. */
static mtx_t g_det_lock;
static uint32_t g_det_due;
static int g_det_waiting;
static _Atomic uint64_t g_det_last_wait;
static uint32_t g_det_main;  /* the main thread's OSThread* */
static volatile int g_det_in_vwait;  /* ... and inside VIWaitForRetrace */
static uint32_t g_det_cur[8][2];
static uint64_t g_det_other_since;  /* main asleep, another thread still running since then */
volatile int g_det_spin;  /* set by code patches inside the frame end's retrace-count loop (patches.txt) */
volatile int g_det_lspin;  /* the thread in the REL loader's disc-data poll loop, 0 = none (patches.txt) */
static volatile int g_det_in_end;  /* the main thread is in the game's frame end (frame_pacing.c) */
void vi_det_frame_end(uint32_t thread, int in) {
    mtx_lock(&g_det_lock);
    g_det_main = thread;
    g_det_in_end = in;
    if (in) { g_det_waiting = 1; atomic_store(&g_det_last_wait, rt_ticks()); }  /* the frame end waits for retraces one way or another */
    else g_det_waiting = 0;
    mtx_unlock(&g_det_lock);
}
static unsigned g_det_n[6];  /* [det] log: delivered, fallback, main running, disc, audio, polls */
/* SR_DETCLOCK: a point where something from outside (a device completing, a retrace) can be handed to
 * the game without its timing showing: no interrupt handler is running, and either no game thread runs
 * (the OS idles) or the one running is parked in a known wait loop (the frame end's retrace-count
 * loop, the REL loader's disc-data loops; code patches mark them) */
static int det_quiet(void) {
    extern _Atomic int g_irq_running;
    extern volatile int g_core_preempt[3];
    if (atomic_load(&g_irq_running) || g_core_preempt[0]) return 0;
    uint32_t cur = ld32(0x800000E4u);
    if (cur == 0) return ld32(OS_CURRENT_CONTEXT) == OS_IDLE_CONTEXT;
    if (cur == (uint32_t)g_det_lspin) return 1;
    return cur == g_det_main && g_det_in_end && g_det_spin;
}
int di_det_busy(void); void di_det_poll(void);
static void det_raise(void) {  /* under g_det_lock: hand one due retrace to the main thread, asleep in its wait */
    if (!g_det_due || !g_det_waiting) return;
    g_det_n[5]++;
    /* OSThread.state (u16 at +0x2C8) 4 = waiting: the main thread has gone to sleep in its retrace wait
     * and is passing through the scheduler with interrupts off (the OS may still name it current) */
    int asleep = g_det_in_vwait && ld16(g_det_main + 0x2C8) == 4;
    if (ld32(0x800000E4u) == g_det_main && !asleep) {
        /* still running: hand it over only if the main thread spins in the frame end itself (the game's
         * 30 fps mode, used while loading, polls the retrace count there until it reaches the last
         * present + 2, without VIWaitForRetrace); code patches mark that loop (g_det_spin) */
        if (!(g_det_in_end && g_det_spin) || !det_quiet()) { g_det_n[2]++; return; }
    } else if (!g_det_in_vwait) {  /* not running, but not asleep in VIWaitForRetrace either (preempted) */
        g_det_n[2]++;
        return;
    } else if (!det_quiet()) {
        /* asleep, but another game thread runs (the REL's loader thread decompressing while a race
         * loads): wait until it blocks too, so how far it got does not depend on the host's timing.
         * After 10 s of host time hand over anyway (a thread that never blocks) and say so. */
        uint64_t now = rt_ticks();
        if (!g_det_other_since) g_det_other_since = now;
        {   /* diagnostics: where the other thread is while held (function last entered, its caller) */
            static uint32_t h[16][3]; static uint64_t last_log;
            uint32_t fn = g_ppc_cur_fn, lr = g_ppc_cur_lr;
            for (int i = 0; i < 16; i++)
                if ((h[i][0] == fn && h[i][1] == lr) || !h[i][2]) { h[i][0] = fn; h[i][1] = lr; h[i][2]++; break; }
            if (now - last_log > TICKS_PER_FIELD * 120) {
                last_log = now;
                char b[512]; int n = 0;
                for (int i = 0; i < 16 && h[i][2]; i++) n += snprintf(b + n, sizeof b - n, " %08X/%08X x%u", h[i][0], h[i][1], h[i][2]);
                rt_log("[det] held for thread %08X:%s", ld32(0x800000E4u), b);
                memset(h, 0, sizeof h);
            }
        }
        if (now - g_det_other_since < TICKS_PER_FIELD * 600) { g_det_n[2]++; return; }
        static unsigned logged;
        if (logged++ < 20) rt_log("[det] retrace handed over while thread %08X still runs (in %08X)", ld32(0x800000E4u), g_ppc_cur_fn);
    }
    {   /* the GPU first: wait until it has done the work already queued and hand its results over (a
         * draw-done / token interrupt) before anything else, as if it were infinitely fast. Otherwise a
         * slower GPU (the Switch) let an audio interrupt in ahead of a token the PC had delivered. */
        size_t gx_gp_backlog(void); int gx_det_deliver(void); int gx_det_wg(void);
        if (gx_gp_backlog()) { g_det_n[2]++; return; }
        /* idle with commands still in the gather buffer: the idle loop (sched.c det_idle) pushes them
         * and delivers their results first */
        if (ld32(0x800000E4u) == 0 && gx_det_wg()) { g_det_n[2]++; return; }
        if (gx_det_deliver()) return;  /* delivered before the next check (det_quiet sees it posted) */
    }
    /* and only once no disc read is in flight (requiring every other thread to be idle too stalled
     * races: a low-priority game thread is always runnable there) */
    int di_det_busy(void); void di_det_poll(void);
    if (di_det_busy()) { g_det_n[3]++; di_det_poll(); return; }  /* complete it now (the main thread sleeps); retrace later */
    int ai_det_try_raise(void);
    if (ai_det_try_raise()) { g_det_n[4]++; return; }  /* this frame's audio interrupts first, one at a time */
    g_det_due--;
    g_det_n[0]++;
    g_det_other_since = 0;
    {   /* diagnostics: which thread runs when a retrace is handed over (0 = all asleep) */
        uint32_t cur = ld32(0x800000E4u);
        for (int i = 0; i < 8; i++)
            if (g_det_cur[i][0] == cur || !g_det_cur[i][1]) { g_det_cur[i][0] = cur; g_det_cur[i][1]++; break; }
    }
    if (g_vi[0x30 >> 1] & 0x1000) {
        g_vi[0x30 >> 1] |= 0x8000;
        os_raise_interrupt(OS_INTERRUPT_PI_VI);
    }
}
void vi_det_frame_done(void) {  /* the main thread's retrace wait returned */
    mtx_lock(&g_det_lock);
    g_det_waiting = 0;
    g_det_in_vwait = 0;
    mtx_unlock(&g_det_lock);
}
/* SR_DETCLOCK: when every game thread is blocked (the OS idles in SelectThread) and the main thread is
 * not in a frame wait (boot, loading screens that wait on audio or timers, a frame waiting for nothing
 * but time), 1/60 s of game time passes there: the clock steps, the frame's audio quota and due alarms
 * are handed over, then a retrace, one at a time while the CPU stays idle. */
static _Atomic uint64_t g_det_last_tick;  /* the last idle tick (the backstop leaves those periods alone) */
static int det_idle_tick(void) {  /* game thread (sched.c det_idle via vi_det_idle); 1: did something */
    static int vi_pending;
    int ppc_detclock(void); uint32_t ppc_detclock_steps(void);
    if (!ppc_detclock()) return 0;
    /* idle: no game thread runs or is ready, and nothing that could wake one is under way (a disc read,
     * GPU work, a GPU interrupt held for delivery, an interrupt raised but not delivered yet). Then the
     * state stays as it is until game time moves, however long the host takes to notice, so a tick here
     * comes at the same point in the game every run. */
    int di_det_busy(void); int gx_det_pending(void);
    size_t gx_gp_backlog(void);
    extern volatile int g_core_preempt[3];
    int idle = ld32(OS_CURRENT_THREAD) == 0 && det_quiet() && ld32(OS_RUNQUEUE_BITS) == 0 && !di_det_busy() &&
               !gx_gp_backlog() && !gx_det_pending() && !g_core_preempt[0];
    if (!idle) return 0;
    int ai_det_try_raise(void);
    if (ai_det_try_raise()) return 1;  /* audio interrupts owed for past frames first */
    if (vi_pending) {  /* the tick's retrace */
        mtx_lock(&g_det_lock);
        if (g_det_due) g_det_due--;
        mtx_unlock(&g_det_lock);
        vi_pending = 0;
        g_det_n[0]++;
        if (g_vi[0x30 >> 1] & 0x1000) {
            g_vi[0x30 >> 1] |= 0x8000;
            os_raise_interrupt(OS_INTERRUPT_PI_VI);
        }
        return 1;
    }
    if (!g_det_due) return 0;  /* one tick per host field: idle periods pass in real time */
    void ppc_detclock_step(void); void ai_det_frame(void); void sched_det_frame(void);
    atomic_store(&g_det_last_tick, rt_ticks());
    ppc_detclock_step();
    ai_det_frame();
    sched_det_frame();
    vi_pending = 1;
    return 1;
}

/* SR_DETCLOCK, game thread, the CPU idle with nothing under way (sched.c det_idle): hand over what is
 * due (audio interrupts, a retrace for the main thread asleep in its frame wait, or an idle tick).
 * While the CPU idles only the game thread hands things over, in one order; the retrace thread just
 * counts retraces due. 1: something was handed over or game time moved. */
int vi_det_idle(void) {
    extern volatile int g_core_preempt[3];
    if (g_det_waiting) {
        mtx_lock(&g_det_lock);
        det_raise();
        mtx_unlock(&g_det_lock);
        return g_core_preempt[0] != 0;
    }
    return det_idle_tick();
}

/* SR_DETCLOCK: game time is moving (a frame wait or an idle tick in the last half second); sched.c's
 * alarm backstop leaves alarms to game time then */
int vi_det_time_moving(void) {
    uint64_t w = atomic_load(&g_det_last_wait), t = atomic_load(&g_det_last_tick);
    return rt_ticks() - (w > t ? w : t) < TICKS_PER_FIELD * 30;
}

void vi_det_frame_wait(uint32_t thread) {
    atomic_store(&g_det_last_wait, rt_ticks());
    mtx_lock(&g_det_lock);
    g_det_main = thread;
    g_det_waiting = 1;
    g_det_in_vwait = 1;
    mtx_unlock(&g_det_lock);
}

static int vi_thread(void* arg) {
    (void)arg;
    rt_thread_pin(2);
    g_field_start = rt_ticks();
    for (;;) {
        uint64_t next = g_field_start + TICKS_PER_FIELD;
        /* far behind (the console slept, or the process was held): carry on from now instead of
         * replaying every missed retrace */
        if ((int64_t)(rt_ticks() - next) > (int64_t)(TICKS_PER_FIELD * 30)) {
            rt_log("[vi] retrace clock %.1f s behind (sleep or suspension): resynchronized",
                   (double)(rt_ticks() - next) / 40500000.0);
            next = rt_ticks();
        }
        for (;;) {
            int64_t left = (int64_t)(next - rt_ticks());
            {   /* SR_DETCLOCK: the GPU's draw-done/token interrupts, at a quiet point: no game thread
                 * running, or only one parked in a known wait loop (frame end, REL loader) */
                int ppc_detclock(void); int gx_det_deliver(void);
                size_t gx_gp_backlog(void);  /* only once the GPU has done all queued work (fast and slow GPUs alike) */
                /* (not while the CPU idles: sched.c det_idle hands everything over then) */
                if (ppc_detclock() && ld32(0x800000E4u) != 0 && det_quiet() && !gx_gp_backlog()) gx_det_deliver();
            }
            if (g_det_waiting && ld32(0x800000E4u) != 0) {  /* SR_DETCLOCK: a due retrace for the main thread */
                mtx_lock(&g_det_lock);
                det_raise();
                mtx_unlock(&g_det_lock);
            }
            if (left <= 0) break;
            struct timespec ts = {0, (long)(left * 2000 / 81)};
            int ai_det_pending(void);
            long cap = g_det_waiting && ai_det_pending() ? 100000 : 2000000;  /* audio interrupts owed: check often */
            if (ts.tv_nsec > cap) ts.tv_nsec = cap;
            thrd_sleep(&ts, NULL);
        }
        g_field_start = next;
        g_fields++;
        { void gx_tl(char type, uint32_t a); gx_tl('V', g_fields); }
        /* SR_DETCLOCK: retraces are handed to the game where its main thread waits (vi_det_frame_wait) */
        int ppc_detclock(void); uint32_t ppc_detclock_steps(void);
        if (ppc_detclock() && g_fields % 600 == 0) {
            rt_log("[det] retraces handed over %u, by the backstop %u; held: main running %u, disc %u, audio %u (of %u checks)",
                   g_det_n[0], g_det_n[1], g_det_n[2], g_det_n[3], g_det_n[4], g_det_n[5]);
            memset(g_det_n, 0, sizeof g_det_n);
            rt_log("[det] running at handover: %08X x%u, %08X x%u, %08X x%u, %08X x%u, %08X x%u",
                   g_det_cur[0][0], g_det_cur[0][1], g_det_cur[1][0], g_det_cur[1][1], g_det_cur[2][0], g_det_cur[2][1],
                   g_det_cur[3][0], g_det_cur[3][1], g_det_cur[4][0], g_det_cur[4][1]);
            memset(g_det_cur, 0, sizeof g_det_cur);
        }
        uint64_t det_active = atomic_load(&g_det_last_wait) > atomic_load(&g_det_last_tick) ? atomic_load(&g_det_last_wait) : atomic_load(&g_det_last_tick);
        /* the backstop waits 10 fields, or 12 s while a game thread is busy running code outside the
         * known wait loops (a slow frame, or a background thread decompressing on a slower machine:
         * the Switch's boot ran past 10 fields where the PC did not; a retrace slipped in mid-work
         * would change the outcome) */
        uint64_t det_limit = TICKS_PER_FIELD * 10;
        {   uint32_t cur = ld32(0x800000E4u);
            if (cur && cur != (uint32_t)g_det_lspin && !(cur == g_det_main && g_det_in_end)) det_limit = TICKS_PER_FIELD * 720; }
        /* nor while the GPU still has queued work: the game waits for it (the first frames wait ~300 ms
         * while the render thread creates its GL context), and no game time passes meanwhile */
        { size_t gx_gp_backlog(void);
          if (gx_gp_backlog()) { atomic_store(&g_det_last_tick, rt_ticks()); } }
        if (atomic_load(&g_det_last_tick) > det_active) det_active = atomic_load(&g_det_last_tick);
        if (ppc_detclock() && rt_ticks() - det_active >= det_limit) {
            g_det_n[1]++;
            static unsigned logged;  /* what the game runs instead of waiting (first 40) */
            if (logged++ < 40) {
                extern volatile int g_det_bt_req;
                g_det_bt_req = (int)ld32(0x800000E4u);  /* that thread logs its call chain (sched.c) */
                int di_det_busy(void); int gx_det_pending(void); size_t gx_gp_backlog(void); int gx_det_parts(void);
                extern volatile int g_core_preempt[3];
                rt_log("[det] backstop: running thread %08X in %08X (lr %08X); main %08X state %u, waiting %d vwait %d end %d due %d; "
                       "ready %08X disc %d gp %d pe %d irq %d ctx %08X",
                       ld32(0x800000E4u), g_ppc_cur_fn, g_ppc_cur_lr, g_det_main, g_det_main ? ld16(g_det_main + 0x2C8) : 0,
                       g_det_waiting, g_det_in_vwait, g_det_in_end, g_det_due, ld32(OS_RUNQUEUE_BITS), di_det_busy(),
                       (int)gx_gp_backlog(), gx_det_pending() | gx_det_parts() << 4, g_core_preempt[0], ld32(OS_CURRENT_CONTEXT));
            }
        }
        if (ppc_detclock() && rt_ticks() - det_active < det_limit) {
            mtx_lock(&g_det_lock);
            if (g_det_due < 2) g_det_due++;
            if (ld32(0x800000E4u) != 0) det_raise();  /* idle: sched.c det_idle hands it over */
            mtx_unlock(&g_det_lock);
            continue;
        }
        /* DI0: the retrace interrupt VIInit configures (enable bit 0x1000, status bit 0x8000) */
        if (g_vi[0x30 >> 1] & 0x1000) {
            g_vi[0x30 >> 1] |= 0x8000;
            os_raise_interrupt(OS_INTERRUPT_PI_VI);
        }
    }
    return 0;
}

void vi_init(void) {
    mtx_init(&g_det_lock, mtx_plain);
    memset((void*)g_vi, 0, sizeof g_vi);
    atomic_store(&g_det_last_tick, rt_ticks());  /* SR_DETCLOCK: the backstop's grace period starts at boot */
    rt_thread_start(vi_thread, NULL);
}
