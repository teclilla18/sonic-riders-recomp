/* CPU-side runtime for recompiled code: dispatch, supervisor state, timebase, diagnostics. */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp_table.h"
#include "runtime.h"

int g_ppc_trace;
volatile int g_core_preempt[3];
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
void ppc_preempt(Cpu* c) { (void)c; }
void ppc_trace_enter(uint32_t addr) { rt_log("[trace] %08X", addr); }
void ppc_cr_poisoned(Cpu* c, int bit, uint32_t addr) { rt_fatal("CR bit %d read after it was dropped, at %08X", bit, addr); (void)c; }

/* ---- timebase: 40.5 MHz (bus clock / 4) ---- */
static uint64_t g_t0;
static uint64_t host_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
uint64_t rt_ticks(void) { return (host_ns() - g_t0) * 81 / 2000; }  /* ns * 40.5e6 / 1e9 */
uint64_t ppc_timebase(void) { return rt_ticks(); }

/* ---- supervisor state ---- */
static uint32_t g_msr = 0x00002032;  /* FP, IR, DR set; EE clear (as at boot) */
static uint32_t g_spr[1024];

uint32_t ppc_mfmsr(Cpu* c) { (void)c; return g_msr; }
void ppc_mtmsr(Cpu* c, uint32_t v) { (void)c; g_msr = v; }  /* EE: the interrupt model comes with the OS layer */
uint32_t ppc_mfspr(Cpu* c, int spr) {
    (void)c;
    if (spr == 287) return 0x00083214;  /* PVR: Gekko */
    return g_spr[spr & 1023];
}
void ppc_mtspr(Cpu* c, int spr, uint32_t v) { (void)c; g_spr[spr & 1023] = v; }
void ppc_rfi(Cpu* c, uint32_t addr) { rt_fatal("rfi at %08X (OS exception code should be replaced natively)", addr); (void)c; }
void ppc_sc(Cpu* c, uint32_t addr) { (void)c; (void)addr; }  /* __OSSystemCall: sync + isync, nothing to do */

void cpu_init(Cpu* c) {
    memset(c, 0, sizeof *c);
    g_t0 = host_ns();
}
