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
#include <threads.h>
#include <time.h>

#include "game_addrs.h"
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
    mtx_lock(&g_lock);
    while (g_running != g) cnd_wait(&g->cv, &g_lock);
    mtx_unlock(&g_lock);
    t_self = g;
    Cpu* c = &g->cpu;
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

/* hand the CPU to `next` and park until this host thread's context is loaded again */
static void handoff(GThread* next) {
    mtx_lock(&g_lock);
    g_running = next;
    if (!next->started) {
        next->started = 1;
        thrd_t t;
        if (thrd_create(&t, thread_main, next) != thrd_success) rt_fatal("cannot create a host thread");
        thrd_detach(t);
    } else {
        cnd_signal(&next->cv);
    }
    while (g_running != t_self) cnd_wait(&t_self->cv, &g_lock);
    mtx_unlock(&g_lock);
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
    ic->r[1] = (c->r[1] - 0x1000) & ~0xFu;
    ic->msr = c->msr & ~MSR_EE;
}

static void deliver(Cpu* c) {
    if (t_in_irq) return;
    t_in_irq = 1;
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
    t_in_irq = 0;
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
void sched_interrupts_enabled(Cpu* c) {
    os_irq_retry();
    if (g_core_preempt[0]) deliver(c);
    if (ld32(OS_CURRENT_CONTEXT) == OS_IDLE_CONTEXT && ld32(OS_RUNQUEUE_BITS) == 0) {
        record_backtrace(c);
        mtx_lock(&g_irq_lock);
        if (!g_core_preempt[0]) {
            struct timespec ts;
            timespec_get(&ts, TIME_UTC);
            ts.tv_nsec += 2000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            cnd_timedwait(&g_irq_cv, &g_irq_lock, &ts);
        }
        mtx_unlock(&g_irq_lock);
        if (g_core_preempt[0]) deliver(c);
    }
}

/* ---- decrementer (OSAlarm's timer) ---- */
static mtx_t g_dec_lock;
static cnd_t g_dec_cv;
static int64_t g_dec_deadline;  /* timebase ticks; INT64_MAX = not armed */

void sched_set_decrementer(uint32_t v) {
    mtx_lock(&g_dec_lock);
    g_dec_deadline = (int64_t)rt_ticks() + (int32_t)v;
    cnd_signal(&g_dec_cv);
    mtx_unlock(&g_dec_lock);
}

uint32_t sched_get_decrementer(void) {
    mtx_lock(&g_dec_lock);
    int64_t d = g_dec_deadline == INT64_MAX ? -1 : g_dec_deadline - (int64_t)rt_ticks();
    mtx_unlock(&g_dec_lock);
    return (uint32_t)d;
}

static int dec_main(void* arg) {
    (void)arg;
    mtx_lock(&g_dec_lock);
    for (;;) {
        if (g_dec_deadline == INT64_MAX) { cnd_wait(&g_dec_cv, &g_dec_lock); continue; }
        int64_t left = g_dec_deadline - (int64_t)rt_ticks();
        if (left <= 0) {
            g_dec_deadline = INT64_MAX;
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
    t_self = &g_boot;
    thrd_t t;
    if (thrd_create(&t, dec_main, NULL) != thrd_success) rt_fatal("cannot start the decrementer thread");
    thrd_detach(t);
}

void sched_dump(void) {
    rt_log("[threads] current %08X, run-queue bits %08X, running ctx %08X", ld32(OS_CURRENT_THREAD),
           ld32(OS_RUNQUEUE_BITS), g_running ? g_running->ctx : 0);
    for (GThread* g = g_threads; g; g = g->next) {
        uint32_t t = g->ctx;
        char bt[160] = "";
        int n = 0;
        for (int i = 0; i < 12 && g->bt[i]; i++) n += snprintf(bt + n, sizeof bt - n, " %08X", g->bt[i]);
        rt_log("[threads]   %08X state %u prio %d queue %08X%s parked at:%s", t, t ? ld16(t + 0x2C8) : 0,
               t ? (int)ld32(t + 0x2D0) : -1, t ? ld32(t + 0x2DC) : 0, g == g_running ? " (running)" : "", bt);
    }
}
