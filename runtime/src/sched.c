/* Threads and interrupts.
 *
 * The game's own OS scheduler runs recompiled (SelectThread, OSResumeThread, mutexes, message
 * queues, alarms: all on the guest's data structures). Only the context switch is native:
 * every guest thread is a host thread, exactly one of which runs guest code at a time.
 *   OSSaveContext(ctx)  returns 0 ("saved"); it only learns which guest context this host thread is.
 *   OSLoadContext(ctx)  hands the CPU to the host thread of ctx (starting it the first time) and
 *                       parks the caller until its own context is loaded again. Returning from
 *                       there is the same as the real resume (OSSaveContext returning 1 makes
 *                       SelectThread return 0; it keeps only r30/r31 in its frame and its callers
 *                       ignore the result).
 * Interrupts (the decrementer for OSAlarm, and native device callbacks) are delivered at function
 * entries and loop back-edges (ppc.h) while the running context has MSR.EE set, on the interrupted
 * thread's guest stack, as on the hardware. The decrementer runs the game's own
 * DecrementerExceptionCallback, whose final OSLoadContext(context) returns here (longjmp).
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <threads.h>
#include <time.h>

#include "game_addrs.h"
#include "gx.h"
#include "runtime.h"

volatile int g_core_preempt[3];  /* [0]: an interrupt is pending */

/* ---- guest threads ---- */
typedef struct GThread {
    uint32_t ctx;        /* guest OSContext* (the OSThread's first member) */
    int started;
    cnd_t cv;
    Cpu cpu;
    uint32_t bt[12];     /* guest backtrace where it last parked (diagnostics) */
    struct GThread* next;
} GThread;

static mtx_t g_lock;
static GThread* g_threads;
static GThread* volatile g_running;
static _Thread_local GThread* t_self;
static GThread g_boot;

static GThread* find_thread(uint32_t ctx) {
    for (GThread* g = g_threads; g; g = g->next)
        if (g->ctx == ctx) return g;
    return NULL;
}

static void load_cpu_from_context(Cpu* c, uint32_t ctx) {
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 32; i++) c->r[i] = ld32(ctx + CTX_GPR + 4 * i);
    uint32_t cr = ld32(ctx + CTX_CR);
    for (int i = 0; i < 32; i++) c->cr[i] = (cr >> (31 - i)) & 1;
    c->lr = ld32(ctx + CTX_LR);
    c->ctr = ld32(ctx + CTX_CTR);
    ppc_mtxer(c, ld32(ctx + CTX_XER));
    for (int i = 0; i < 8; i++) c->gqr[i] = ld32(ctx + CTX_GQR + 4 * i);
    c->msr = ld32(ctx + CTX_SRR1);
    c->pc = ld32(ctx + CTX_SRR0);
}

static int thread_main(void* arg) {
    GThread* g = arg;
    rt_thread_pin(0);
    profile_register_thread();
    mtx_lock(&g_lock);
    while (g_running != g) cnd_wait(&g->cv, &g_lock);
    mtx_unlock(&g_lock);
    t_self = g;
    Cpu* c = &g->cpu;
    ppc_enter_guest_thread();  /* the Switch's generated code reads the memory base from x28 */
    load_cpu_from_context(c, g->ctx);
    uint32_t ret_to = c->lr;  /* OSCreateThread sets lr = OSExitThread */
    rt_log("[thread] start guest thread %08X at %08X (stack %08X, prio %d)", g->ctx, c->pc, c->r[1],
           (int)ld32(g->ctx + 0x2D0));
    ppc_dispatch(c);
    /* the thread function returned: its blr goes to OSExitThread with the result in r3 */
    c->pc = ret_to;
    ppc_dispatch(c);
    rt_fatal("guest thread %08X continued after OSExitThread", g->ctx);
}

/* SR_DETCLOCK diagnostics: vi.c asks (after a retrace backstop) and the game thread logs its own call
 * chain the next time it enables interrupts */
volatile int g_det_bt_req;
static void det_bt_log(const Cpu* c) {
    uint32_t chain[8] = {0}, sp = c->r[1];
    for (int i = 0; i < 8 && sp >= 0x80000000u && sp < 0x80000000u + GUEST_RAM_SIZE - 8; i++) {
        uint32_t up = ld32(sp);
        if (up <= sp || up >= 0x80000000u + GUEST_RAM_SIZE - 8) break;
        chain[i] = ld32(up + 4);
        sp = up;
    }
    rt_log("[det] after backstop: thread %08X lr %08X chain %08X %08X %08X %08X %08X %08X %08X %08X",
           ld32(0x800000E4u), c->lr, chain[0], chain[1], chain[2], chain[3], chain[4], chain[5], chain[6], chain[7]);
}

/* hand the CPU to `next` and park until this host thread's context is loaded again */
static void handoff(GThread* next) {
    uint32_t prof_fn = g_ppc_cur_fn;
    g_ppc_cur_fn = PROF_SWITCH;
    mtx_lock(&g_lock);
    g_running = next;
    if (!next->started) {
        next->started = 1;
        rt_thread_start(thread_main, next);
    } else {
        cnd_signal(&next->cv);
    }
    while (g_running != t_self) cnd_wait(&t_self->cv, &g_lock);
    mtx_unlock(&g_lock);
    /* this thread runs again. It returns through the scheduler into code that enters no new function
     * for a while, so the profiler would keep charging that time to the scheduler's last call: charge
     * it to "resumed thread" with the first return address outside the OS thread code instead */
    (void)prof_fn;
    uint32_t ret = 0;
    for (int i = 0; i < 12 && t_self->bt[i]; i++)
        if (t_self->bt[i] < 0x8000F000u || t_self->bt[i] >= 0x80012000u) { ret = t_self->bt[i]; break; }
    g_ppc_cur_fn = PROF_RESUMED;
    g_ppc_cur_lr = ret;
}

/* where this thread parks: the guest stack's back chain (frame word 0 = caller's r1, +4 = LR) */
static void record_backtrace(const Cpu* c) {
    memset(t_self->bt, 0, sizeof t_self->bt);
    uint32_t sp = c->r[1];
    for (int i = 0; i < 12 && sp >= 0x80000000u && sp < 0x80000000u + GUEST_RAM_SIZE; i++) {
        uint32_t up = ld32(sp);
        if (up <= sp || up >= 0x80000000u + GUEST_RAM_SIZE) break;
        t_self->bt[i] = ld32(up + 4);
        sp = up;
    }
}

void hle_OSSaveContext(Cpu* c) {
    uint32_t ctx = c->r[3];
    if (!t_self->ctx) {
        t_self->ctx = ctx;
    } else if (t_self->ctx != ctx) {
        rt_fatal("OSSaveContext(%08X) on the host thread of guest context %08X", ctx, t_self->ctx);
    }
    c->r[3] = 0;
}

/* exception returns: DecrementerExceptionCallback ends with OSLoadContext(interrupted context) */
#define MAX_EXC 8
static _Thread_local struct { uint32_t ctx; jmp_buf jb; } t_exc[MAX_EXC];
static _Thread_local int t_exc_depth;

void hle_OSLoadContext(Cpu* c) {
    uint32_t ctx = c->r[3];
    if (t_exc_depth > 0 && t_exc[t_exc_depth - 1].ctx == ctx) longjmp(t_exc[t_exc_depth - 1].jb, 1);
    GThread* g = find_thread(ctx);
    if (g == t_self) return;  /* this thread was selected again: resume where it is */
    record_backtrace(c);
    if (!g) {
        g = calloc(1, sizeof *g);
        g->ctx = ctx;
        cnd_init(&g->cv);
        mtx_lock(&g_lock);
        g->next = g_threads;
        g_threads = g;
        mtx_unlock(&g_lock);
    }
    handoff(g);
}

/* ---- interrupts ---- */
static _Atomic uint32_t g_irq_bits;
static mtx_t g_irq_lock;
static cnd_t g_irq_cv;
typedef struct { void (*fn)(Cpu*, void*); void* arg; } NativeIrq;
#define NATIVE_IRQ_MAX 256
static NativeIrq g_native[NATIVE_IRQ_MAX];
static unsigned g_native_head, g_native_tail;

void rt_irq_raise(uint32_t bits) {
    mtx_lock(&g_irq_lock);
    g_irq_bits |= bits;
    g_core_preempt[0] = 1;
    cnd_broadcast(&g_irq_cv);
    mtx_unlock(&g_irq_lock);
}

void rt_irq_post(void (*fn)(Cpu*, void*), void* arg) {
    mtx_lock(&g_irq_lock);
    if (g_native_tail - g_native_head >= NATIVE_IRQ_MAX) rt_fatal("native interrupt queue full");
    g_native[g_native_tail++ % NATIVE_IRQ_MAX] = (NativeIrq){fn, arg};
    g_irq_bits |= RT_IRQ_NATIVE;
    g_core_preempt[0] = 1;
    cnd_broadcast(&g_irq_cv);
    mtx_unlock(&g_irq_lock);
}

/* call a guest function on c (returns when it returns) */
void guest_call(Cpu* c, uint32_t addr) {
    c->lr = 0;
    c->pc = addr;
    ppc_dispatch(c);
}

static void run_decrementer(Cpu* ic) {
    if (t_exc_depth >= MAX_EXC) rt_fatal("exceptions nested too deeply");
    uint32_t ctx = ld32(OS_CURRENT_CONTEXT);
    int d = t_exc_depth++;
    t_exc[d].ctx = ctx;
    if (!setjmp(t_exc[d].jb)) {
        ic->r[3] = 8;  /* __OS_EXCEPTION_DECREMENTER */
        ic->r[4] = ctx;
        guest_call(ic, FN_DecrementerExceptionCallback);
        rt_log("[irq] DecrementerExceptionCallback returned without OSLoadContext");
    }
    t_exc_depth = d;
}

static _Thread_local int t_in_irq;

/* interrupt context: a copy of the interrupted registers on the same guest stack, below the
 * interrupted frame (the struct's r1 may lag behind a leaf function's frame: keep a margin) */
static void make_irq_cpu(Cpu* ic, const Cpu* c) {
    *ic = *c;
    /* the handlers' frames go just below the interrupted code's stack pointer, as on the hardware;
     * the old 4 KB gap ran them off the bottom of small thread stacks (CRI's loader threads have
     * 4 KB) and corrupted the memory below: story mode's loading-screen text table. Code that keeps
     * r1 in a local hands it over at interrupt checks (PPC_LOOP_R1). */
    ic->r[1] = (c->r[1] - 0x100) & ~0xFu;
    ic->msr = c->msr & ~MSR_EE;
}

/* SR_DETCLOCK: interrupt handlers are running on the game thread. Set before the pending flag is
 * cleared and cleared after the handlers ran (a thread they woke is in the run queue by then), so
 * vi.c never sees "no thread running, nothing pending" in between */
_Atomic int g_irq_running;
static void deliver(Cpu* c) {
    if (t_in_irq) return;
    t_in_irq = 1;
    atomic_store(&g_irq_running, 1);
    /* the profiler charges the time after the handlers to the interrupted code again */
    uint32_t prof_fn = g_ppc_cur_fn, prof_lr = g_ppc_cur_lr;
    while (g_core_preempt[0] && (c->msr & MSR_EE)) {
        mtx_lock(&g_irq_lock);
        g_core_preempt[0] = 0;
        uint32_t bits = g_irq_bits;
        g_irq_bits = 0;
        unsigned head = g_native_head, tail = g_native_tail;
        g_native_head = tail;
        mtx_unlock(&g_irq_lock);
        Cpu ic;
        make_irq_cpu(&ic, c);
        c->msr &= ~MSR_EE;
        if (bits & RT_IRQ_DEC) run_decrementer(&ic);
        for (unsigned i = head; i != tail; i++) {
            NativeIrq n = g_native[i % NATIVE_IRQ_MAX];
            make_irq_cpu(&ic, c);
            n.fn(&ic, n.arg);
        }
        c->msr |= MSR_EE;
    }
    atomic_store(&g_irq_running, 0);
    t_in_irq = 0;
    g_ppc_cur_fn = prof_fn;
    g_ppc_cur_lr = prof_lr;
    /* the handlers may have written GX commands (a retrace callback); while the CPU idles nothing else
     * pushes them to the GP (SR_DETCLOCK counts them as GPU work under way) */
    if (ld32(OS_CURRENT_CONTEXT) == OS_IDLE_CONTEXT) gx_pipe_idle();
    /* a thread made ready by the interrupt may preempt the interrupted one */
    if (ld32(OS_RUNQUEUE_HINT) && ld32(OS_CURRENT_THREAD) && ld32(OS_CURRENT_CONTEXT) == ld32(OS_CURRENT_THREAD)) {
        Cpu ic;
        make_irq_cpu(&ic, c);
        guest_call(&ic, FN_OSReschedule);
    }
}

void ppc_preempt(Cpu* c) {
    if (c->msr & MSR_EE) deliver(c);
}

/* MSR.EE turned on (OSEnableInterrupts / OSRestoreInterrupts): deliver what is pending. In the
 * scheduler's idle loop (no ready thread), wait for an interrupt instead of spinning. */
/* for the [perf] breakdown: time idle (no ready guest thread), decrementer lateness */
static _Atomic uint64_t g_idle_ticks, g_dec_late_sum, g_dec_late_max, g_dec_fired;

static _Atomic uint64_t g_idle_total;  /* never reset ([hitch] lines) */
uint64_t sched_idle_total(void) { return atomic_load(&g_idle_total); }

void sched_stats(uint64_t* idle, uint64_t* late_avg, uint64_t* late_max) {
    uint64_t n = atomic_exchange(&g_dec_fired, 0);
    *idle = atomic_exchange(&g_idle_ticks, 0);
    *late_avg = n ? atomic_exchange(&g_dec_late_sum, 0) / n : 0;
    *late_max = atomic_exchange(&g_dec_late_max, 0);
}

/* idle accounting for [perf]: time with no ready guest thread, and the part of it while the GP thread
 * still had queued FIFO data (the game waiting for the GP) */
static uint64_t g_spin_last, g_spin_idle, g_spin_idle_gp;
size_t gx_gp_backlog(void);
void sched_idle_split(uint64_t* idle, uint64_t* waiting_gp) {
    *idle = g_spin_idle; *waiting_gp = g_spin_idle_gp;
    g_spin_idle = g_spin_idle_gp = 0;
}

void sched_interrupts_enabled(Cpu* c) {
    if (g_det_bt_req && (uint32_t)g_det_bt_req == ld32(0x800000E4u)) { g_det_bt_req = 0; det_bt_log(c); }
    {
        uint64_t now = rt_ticks();
        if (ld32(OS_RUNQUEUE_BITS) == 0) {
            if (g_spin_last && now - g_spin_last < 40500) {  /* consecutive idle checks (< 1 ms apart) */
                g_spin_idle += now - g_spin_last;
                if (gx_gp_backlog()) g_spin_idle_gp += now - g_spin_last;
            }
            g_spin_last = now;
        } else {
            g_spin_last = 0;
        }
    }
    os_irq_retry();
    if (g_core_preempt[0]) deliver(c);
    if (ld32(OS_CURRENT_CONTEXT) == OS_IDLE_CONTEXT && ld32(OS_RUNQUEUE_BITS) == 0) {
        { void di_det_poll(void); di_det_poll(); }  /* SR_DETCLOCK: a disc command completes when the CPU idles */
        { int gx_det_deliver(void); gx_det_deliver(); }  /* SR_DETCLOCK: and the GPU's interrupts arrive */
        if (g_core_preempt[0]) { deliver(c); return; }
        record_backtrace(c);
        uint32_t prof_prev = g_ppc_cur_fn;
        g_ppc_cur_fn = PROF_IDLE;
        gx_pipe_idle();
        void gx_tl(char type, uint32_t a);
        gx_tl('I', 0);
        uint64_t t_idle = rt_ticks();
        mtx_lock(&g_irq_lock);
        if (!g_core_preempt[0]) {
            struct timespec ts;
            timespec_get(&ts, TIME_UTC);
            ts.tv_nsec += 2000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            cnd_timedwait(&g_irq_cv, &g_irq_lock, &ts);
        }
        mtx_unlock(&g_irq_lock);
        uint64_t d = rt_ticks() - t_idle;
        gx_tl('i', (uint32_t)(d * 1000 / 40500));
        g_spin_idle += d;
        if (gx_gp_backlog()) g_spin_idle_gp += d;
        g_spin_last = 0;
        g_idle_ticks += d;
        g_idle_total += d;
        g_ppc_cur_fn = prof_prev;
        if (g_core_preempt[0]) deliver(c);
    }
}

/* ---- decrementer (OSAlarm's timer) ---- */
static mtx_t g_dec_lock;
static cnd_t g_dec_cv;
static int64_t g_dec_deadline;  /* timebase ticks; INT64_MAX = not armed */

/* SR_DETCLOCK (online play groundwork): once game frames run, the decrementer counts game time
 * (cpu.c's frame-locked time base) and fires at a frame end (sched_det_frame); the host-clock
 * deadline stays only as a backstop, 50 ms after the real one (loading without frames) */
static int64_t g_dec_vdeadline = INT64_MAX;
static int det_dec(void) {
    int ppc_detclock(void); uint32_t ppc_detclock_steps(void);
    return ppc_detclock();
}
static int64_t det_now(void) { uint64_t ppc_detclock_base(void); return (int64_t)ppc_detclock_base(); }
void sched_det_frame(void) {
    mtx_lock(&g_dec_lock);
    int fire = g_dec_vdeadline != INT64_MAX && det_now() >= g_dec_vdeadline;
    if (fire) { g_dec_vdeadline = INT64_MAX; g_dec_deadline = INT64_MAX; }
    mtx_unlock(&g_dec_lock);
    if (fire) rt_irq_raise(RT_IRQ_DEC);
}

void sched_set_decrementer(uint32_t v) {
    mtx_lock(&g_dec_lock);
    if (det_dec()) {
        g_dec_vdeadline = det_now() + (int32_t)v;
        g_dec_deadline = (int64_t)rt_ticks() + (int32_t)v + 40500000 / 20;  /* backstop: 50 ms late on the host clock */
        cnd_signal(&g_dec_cv);
        mtx_unlock(&g_dec_lock);
        return;
    }
    g_dec_vdeadline = INT64_MAX;
    g_dec_deadline = (int64_t)rt_ticks() + (int32_t)v;
    cnd_signal(&g_dec_cv);
    mtx_unlock(&g_dec_lock);
}

uint32_t sched_get_decrementer(void) {
    mtx_lock(&g_dec_lock);
    int64_t d = g_dec_vdeadline != INT64_MAX ? g_dec_vdeadline - det_now()
              : g_dec_deadline == INT64_MAX ? -1 : g_dec_deadline - (int64_t)rt_ticks();
    mtx_unlock(&g_dec_lock);
    return (uint32_t)d;
}

static int dec_main(void* arg) {
    (void)arg;
    rt_thread_pin(2);
    mtx_lock(&g_dec_lock);
    for (;;) {
        if (g_dec_deadline == INT64_MAX) { cnd_wait(&g_dec_cv, &g_dec_lock); continue; }
        int64_t left = g_dec_deadline - (int64_t)rt_ticks();
        if (left <= 0 && g_dec_vdeadline != INT64_MAX) {
            /* SR_DETCLOCK: while game time moves (frames, idle ticks) the alarm waits for it (it fires at
             * a frame end); the host-clock backstop is only for stretches where game time stands still */
            int vi_det_time_moving(void);
            if (vi_det_time_moving()) {
                g_dec_deadline = (int64_t)rt_ticks() + 40500000 / 20;
                continue;
            }
        }
        if (left <= 0) {
            uint64_t late = (uint64_t)-left;
            g_dec_late_sum += late;
            if (late > g_dec_late_max) g_dec_late_max = late;
            g_dec_fired++;
            if (g_dec_vdeadline != INT64_MAX) {  /* SR_DETCLOCK: the host-clock backstop fired first */
                static unsigned logged;
                if (logged++ < 20) rt_log("[det] alarm backstop: decrementer fired on the host clock (game time %lld ticks short)",
                                          (long long)(g_dec_vdeadline - det_now()));
            }
            g_dec_deadline = INT64_MAX;
            g_dec_vdeadline = INT64_MAX;
            mtx_unlock(&g_dec_lock);
            rt_irq_raise(RT_IRQ_DEC);
            mtx_lock(&g_dec_lock);
            continue;
        }
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        int64_t ns = left * 2000 / 81;  /* ticks at 40.5 MHz -> ns */
        ts.tv_sec += ns / 1000000000;
        ts.tv_nsec += ns % 1000000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        cnd_timedwait(&g_dec_cv, &g_dec_lock, &ts);
    }
    return 0;
}

void sched_init(void) {
    mtx_init(&g_lock, mtx_plain);
    mtx_init(&g_irq_lock, mtx_plain);
    cnd_init(&g_irq_cv);
    mtx_init(&g_dec_lock, mtx_plain);
    cnd_init(&g_dec_cv);
    g_dec_deadline = INT64_MAX;
    cnd_init(&g_boot.cv);
    g_boot.started = 1;
    g_threads = &g_boot;
    g_running = &g_boot;
    rt_thread_start(dec_main, NULL);
}

void di_dump(void);
void os_irq_dump(void);

void render_dump(void);

/* a ring of recent events for hang dumps: 1 OSResumeThread(a = thread, b = caller), 2 OSSuspendThread,
 * 3 DVD command started (a = disc offset, b = length), 4 DVD command completed, 5 DVD interrupt
 * delivered; the suspend count of a thread is recorded at the call */
#define DBG_EVENTS 65536
static struct { uint64_t t; int kind; uint32_t a, b, susp; } g_dbg_ev[DBG_EVENTS];
static _Atomic unsigned g_dbg_pos;

/* the same events without those of threads that produced more than 30 a second for 10 seconds in a
 * row (periodic threads; a long hang otherwise leaves only theirs in the ring) */
#define DBG_QUIET 4096
static struct { uint64_t t; int kind; uint32_t a, b, susp; } g_dbg_q[DBG_QUIET];
static unsigned g_dbg_qpos;
static struct { uint32_t id, n, busy, streak; uint64_t t0; } g_dbg_rate[32];
static mtx_t g_dbg_qlock;
static once_flag g_dbg_qonce = ONCE_FLAG_INIT;
static void dbg_qinit(void) { mtx_init(&g_dbg_qlock, mtx_plain); }

void dbg_event(int kind, uint32_t a, uint32_t b) {
    unsigned i = atomic_fetch_add(&g_dbg_pos, 1) % DBG_EVENTS;
    uint64_t t = rt_ticks();
    uint32_t susp = (kind == 1 || kind == 2) && a >= 0x80000000u && a < 0x80000000u + GUEST_RAM_SIZE - 0x300 ? ld32(a + 0x2CC) : 0;
    g_dbg_ev[i].t = t;
    g_dbg_ev[i].kind = kind;
    g_dbg_ev[i].a = a;
    g_dbg_ev[i].b = b;
    g_dbg_ev[i].susp = susp;
    call_once(&g_dbg_qonce, dbg_qinit);
    mtx_lock(&g_dbg_qlock);
    int keep = 1;
    if (kind <= 2) {
        int j = 0;
        while (j < 32 && g_dbg_rate[j].id && g_dbg_rate[j].id != a) j++;
        if (j < 32) {
            if (!g_dbg_rate[j].id) { g_dbg_rate[j].id = a; g_dbg_rate[j].t0 = t; }
            if (t - g_dbg_rate[j].t0 > 40500000u) {
                g_dbg_rate[j].streak = g_dbg_rate[j].n > 30 ? g_dbg_rate[j].streak + 1 : 0;
                if (g_dbg_rate[j].streak >= 10) g_dbg_rate[j].busy = 1;
                g_dbg_rate[j].t0 = t;
                g_dbg_rate[j].n = 0;
            }
            g_dbg_rate[j].n++;
            keep = !g_dbg_rate[j].busy;
        }
    }
    if (keep) {
        unsigned q = g_dbg_qpos++ % DBG_QUIET;
        g_dbg_q[q].t = t; g_dbg_q[q].kind = kind; g_dbg_q[q].a = a; g_dbg_q[q].b = b; g_dbg_q[q].susp = susp;
    }
    mtx_unlock(&g_dbg_qlock);
}

static void dbg_quiet_dump(void) {
    static const char* const names[] = {"?", "resume", "suspend", "dvd start", "dvd done", "dvd irq"};
    unsigned end = g_dbg_qpos, from = end > 150 ? end - 150 : 0;
    int64_t now = (int64_t)rt_ticks();
    rt_log("[events] quiet ring (periodic threads left out):");
    for (int j = 0; j < 32 && g_dbg_rate[j].id; j++)
        if (g_dbg_rate[j].busy) rt_log("[events]   busy thread %08X", g_dbg_rate[j].id);
    for (unsigned k = from; k < end; k++) {
        unsigned i = k % DBG_QUIET;
        rt_log("[events]   -%10.3f %-9s %08X %08X%s", (double)(now - (int64_t)g_dbg_q[i].t) * 1000.0 / 40500000.0,
               names[g_dbg_q[i].kind < 6 ? g_dbg_q[i].kind : 0], g_dbg_q[i].a, g_dbg_q[i].b,
               g_dbg_q[i].kind <= 2 ? (g_dbg_q[i].susp ? " (was suspended)" : "") : "");
    }
}

/* the last 100 DVD events and suspends/resumes of threads other than the two busiest (CRI's audio
 * threads cycle thousands of times a second) */
static void dbg_events_dump(void) {
    static const char* const names[] = {"?", "resume", "suspend", "dvd start", "dvd done", "dvd irq"};
    unsigned end = atomic_load(&g_dbg_pos), begin = end > DBG_EVENTS ? end - DBG_EVENTS : 0;
    uint32_t busy[2] = {0, 0};
    {
        uint32_t ids[16] = {0};
        unsigned cnt[16] = {0};
        for (unsigned k = begin; k < end; k++) {
            unsigned i = k % DBG_EVENTS;
            if (g_dbg_ev[i].kind > 2) continue;
            int j = 0;
            while (j < 16 && ids[j] && ids[j] != g_dbg_ev[i].a) j++;
            if (j < 16) { ids[j] = g_dbg_ev[i].a; cnt[j]++; }
        }
        for (int r = 0; r < 2; r++) {
            int best = -1;
            for (int j = 0; j < 16; j++)
                if (ids[j] && ids[j] != busy[0] && (best < 0 || cnt[j] > cnt[best])) best = j;
            if (best >= 0) busy[r] = ids[best];
        }
    }
    unsigned shown = 0, from = end;
    while (from > begin && shown < 100) {
        unsigned i = (from - 1) % DBG_EVENTS;
        if (g_dbg_ev[i].kind > 2 || (g_dbg_ev[i].a != busy[0] && g_dbg_ev[i].a != busy[1])) shown++;
        from--;
    }
    int64_t now = (int64_t)rt_ticks();
    rt_log("[events] DVD and thread suspend/resume (not %08X %08X), oldest first; ms before the dump:", busy[0], busy[1]);
    for (unsigned k = from; k < end; k++) {
        unsigned i = k % DBG_EVENTS;
        if (g_dbg_ev[i].kind <= 2 && (g_dbg_ev[i].a == busy[0] || g_dbg_ev[i].a == busy[1])) continue;
        rt_log("[events]   -%10.3f %-9s %08X %08X%s", (double)(now - (int64_t)g_dbg_ev[i].t) * 1000.0 / 40500000.0,
               names[g_dbg_ev[i].kind < 6 ? g_dbg_ev[i].kind : 0], g_dbg_ev[i].a, g_dbg_ev[i].b,
               g_dbg_ev[i].kind <= 2 ? (g_dbg_ev[i].susp ? " (was suspended)" : "") : "");
    }
}

void dsp_dump(void);

void sched_dump(void) {
    dbg_events_dump();
    dbg_quiet_dump();
    profile_dump();
    render_dump();
    di_dump();
    dsp_dump();
    os_irq_dump();
    rt_log("[threads] current %08X, run-queue bits %08X, running ctx %08X", ld32(OS_CURRENT_THREAD),
           ld32(OS_RUNQUEUE_BITS), g_running ? g_running->ctx : 0);
    for (GThread* g = g_threads; g; g = g->next) {
        uint32_t t = g->ctx;
        char bt[160] = "";
        int n = 0;
        for (int i = 0; i < 12 && g->bt[i]; i++) n += snprintf(bt + n, sizeof bt - n, " %08X", g->bt[i]);
        rt_log("[threads]   %08X state %u suspend %d prio %d queue %08X%s parked at:%s", t, t ? ld16(t + 0x2C8) : 0,
               t ? (int)ld32(t + 0x2CC) : 0, t ? (int)ld32(t + 0x2D0) : -1, t ? ld32(t + 0x2DC) : 0,
               g == g_running ? " (running)" : "", bt);
    }
}

/* the calling host thread runs the game's boot code (the default thread until __OSThreadInit) */
void sched_enter_boot_thread(void) { t_self = &g_boot; }
