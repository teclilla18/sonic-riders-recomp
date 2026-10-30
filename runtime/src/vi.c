/* Video Interface: a register model so the game's own VI library (VIInit, VIConfigure,
 * VIWaitForRetrace, retrace callbacks, __VIRetraceHandler) runs recompiled.
 * A host thread raises the retrace interrupt (DI0) at the NTSC field rate, 59.94 Hz. */
#include <stdatomic.h>
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
static void det_raise(void) {  /* under g_det_lock: hand one due retrace to the main thread, asleep in its wait */
    if (!g_det_due || !g_det_waiting) return;
    g_det_n[5]++;
    if (ld32(0x800000E4u) == g_det_main) {
        /* still running: hand it over only if the main thread spins in the frame end itself (the game's
         * 30 fps mode, used while loading, polls the retrace count there until it reaches the last
         * present + 2, without VIWaitForRetrace); seen there on two checks in a row, not passing through */
        static int seen;
        int spinning = g_det_in_end && g_ppc_cur_fn == 0x8000610Cu;
        if (!spinning || !seen) { seen = spinning; g_det_n[2]++; return; }
    } else if (!g_det_in_vwait) {  /* not running, but not asleep in VIWaitForRetrace either (preempted) */
        g_det_n[2]++;
        return;
    }
    /* and only once no disc read is in flight (requiring every other thread to be idle too stalled
     * races: a low-priority game thread is always runnable there) */
    int di_det_busy(void); void di_det_poll(void);
    if (di_det_busy()) { g_det_n[3]++; di_det_poll(); return; }  /* complete it now (the main thread sleeps); retrace later */
    int ai_det_try_raise(void);
    if (ai_det_try_raise()) { g_det_n[4]++; return; }  /* this frame's audio interrupts first, one at a time */
    g_det_due--;
    g_det_n[0]++;
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
            if (g_det_waiting) {  /* SR_DETCLOCK: a due retrace for the main thread once it sleeps */
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
        }
        if (ppc_detclock() && ppc_detclock_steps() > 0 && rt_ticks() - atomic_load(&g_det_last_wait) >= TICKS_PER_FIELD * 10) {
            g_det_n[1]++;
            static unsigned logged;  /* what the game runs instead of waiting (first 40) */
            if (logged++ < 40) rt_log("[det] backstop: running thread %08X in %08X (lr %08X)", ld32(0x800000E4u), g_ppc_cur_fn, g_ppc_cur_lr);
        }
        if (ppc_detclock() && ppc_detclock_steps() > 0 && rt_ticks() - atomic_load(&g_det_last_wait) < TICKS_PER_FIELD * 10) {
            mtx_lock(&g_det_lock);
            if (g_det_due < 2) g_det_due++;
            det_raise();
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
    rt_thread_start(vi_thread, NULL);
}
