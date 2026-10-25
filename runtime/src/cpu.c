/* CPU-side runtime for recompiled code: dispatch, supervisor state, timebase, diagnostics. */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp_table.h"
#include "runtime.h"

int g_ppc_trace;
volatile uint32_t g_ppc_cur_fn, g_ppc_cur_lr;
uint8_t* g_mod_hook_flags;

/* ---- dispatch: guest address -> recompiled function ---- */
void ppc_dispatch(Cpu* c) {
    uint32_t a = c->pc;
    unsigned lo = 0, hi = g_recomp_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (g_recomp_funcs[mid].addr < a) lo = mid + 1; else hi = mid;
    }
    if (lo < g_recomp_func_count && g_recomp_funcs[lo].addr == a) {
        g_recomp_funcs[lo].fn(c);
        return;
    }
    rt_fatal("dispatch to %08X: not a function entry (lr %08X)", a, c->lr);
}

void ppc_host_call(Cpu* c, PpcFunc fn) { fn(c); }
void ppc_mod_run(Cpu* c) { ppc_dispatch(c); }

void ppc_unimplemented(Cpu* c, uint32_t addr, uint32_t insn) {
    rt_fatal("unimplemented instruction %08X at %08X (lr %08X)", insn, addr, c->lr);
}
void ppc_trap(Cpu* c, uint32_t addr) { rt_fatal("trap at %08X (lr %08X)", addr, c->lr); }
/* function-entry ring buffer (SR_WATCHDOG=seconds turns it on; dumped by the watchdog) */
#define TRACE_RING 4096
static volatile uint32_t g_trace_ring[TRACE_RING];
static volatile unsigned g_trace_pos;
void ppc_trace_enter(uint32_t addr) { g_trace_ring[g_trace_pos++ % TRACE_RING] = addr; }
void cpu_dump_trace(void) {
    unsigned p = g_trace_pos;
    enum { SHOW = 256 };
    rt_log("[trace] last %u function entries (oldest first):", SHOW);
    for (unsigned i = TRACE_RING - SHOW; i < TRACE_RING; i++) {
        uint32_t a = g_trace_ring[(p + i) % TRACE_RING];
        if (a) rt_log("[trace]   %08X", a);
    }
}
void ppc_cr_poisoned(Cpu* c, int bit, uint32_t addr) { rt_fatal("CR bit %d read after it was dropped, at %08X", bit, addr); (void)c; }

/* ---- timebase: 40.5 MHz (bus clock / 4) ---- */
static uint64_t g_t0;
/* a cheap monotonic clock (platform_now_ns): the game reads the time base constantly (mftb, OSGetTime)
 * and the renderer times its work; on the Switch, CLOCK_REALTIME went through the time service */
static uint64_t host_ns(void) { return platform_now_ns(); }
uint64_t rt_ticks(void) { return (host_ns() - g_t0) * 81 / 2000; }  /* ns * 40.5e6 / 1e9 */
uint64_t ppc_timebase(void) { return rt_ticks(); }

/* ---- supervisor state ---- */
static uint32_t g_spr[1024];

uint32_t ppc_mfmsr(Cpu* c) { return c->msr; }
void ppc_mtmsr(Cpu* c, uint32_t v) {
    uint32_t old = c->msr;
    c->msr = v;
    if (!(old & 0x8000u) && (v & 0x8000u)) sched_interrupts_enabled(c);
}
uint32_t ppc_mfspr(Cpu* c, int spr) {
    (void)c;
    if (spr == 287) return 0x00083214;  /* PVR: Gekko */
    if (spr == 22) return sched_get_decrementer();
    return g_spr[spr & 1023];
}
void ppc_mtspr(Cpu* c, int spr, uint32_t v) {
    (void)c;
    if (spr == 22) sched_set_decrementer(v);  /* DEC: OSAlarm's timer */
    g_spr[spr & 1023] = v;
}
void ppc_rfi(Cpu* c, uint32_t addr) { rt_fatal("rfi at %08X (OS exception code should be replaced natively)", addr); (void)c; }
void ppc_sc(Cpu* c, uint32_t addr) { (void)c; (void)addr; }  /* __OSSystemCall: sync + isync, nothing to do */

void cpu_init(Cpu* c) {
    memset(c, 0, sizeof *c);
    c->msr = 0x00002032;  /* FP, IR, DR set; EE clear (as at boot) */
    g_t0 = host_ns();
}

/* the single-precision tracking's checking build (ppc.h ppc_single_chk): log the first reports */
void ppc_single_report(uint32_t addr, double v) {
    static int n;
    if (n < 50) {
        n++;
        uint64_t b;
        memcpy(&b, &v, 8);
        rt_log("[recomp] operand at %08X is not single precision (%016llX): singleflow.py is wrong here", addr,
               (unsigned long long)b);
    }
}
