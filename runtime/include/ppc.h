/* Forked from ZeldaWWHDRecomp runtime/include/ppc.h (commit f10b566), MPL-2.0. Modified for GameCube; see NOTES.md. */
/* Espresso (Wii U PowerPC) CPU state and helpers used by recompiled code.
 *
 * Guest memory is a 4 GiB window mapped at a fixed host address, so a guest
 * effective address converts to a host pointer with a single add.
 */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

/* everything declared here is defined inside the program (a static executable): hidden visibility
 * lets position-independent code (the Switch's PIE) address the globals the generated code touches
 * at every function entry (g_ppc_cur_fn, g_core_preempt...) directly instead of through the GOT */
#if defined(__GNUC__) && !defined(_WIN32)
#pragma GCC visibility push(hidden)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* GameCube guest memory (runtime/src/memory.c). The window covers guest *physical* addresses:
 *   0x00000000-0x017FFFFF  MEM1, 24 MiB (seen by the game at 0x80000000 cached / 0xC0000000 uncached)
 *   0x20000000-0x20003FFF  the 16 KiB locked L1 cache (0xE0000000)
 * An effective address becomes physical with ea & 0x3FFFFFFF, which folds both RAM mirrors.
 * 0xC8000000-0xCFFFFFFF (EFB, hardware registers, the GX write-gather pipe at 0xCC008000) never
 * reaches the window: loads and stores there go to the runtime's device code (mmio.c). */
#if defined(__SWITCH__) && defined(__aarch64__) && !defined(__cplusplus) && defined(PPC_GEN_CODE) && defined(PPC_BASE_X28)
extern uint8_t* ppc_mem_base_var;
/* Switch, generated code (configure.py): the base lives in x28 for the whole life of every guest
 * thread (as SwitchWakerHD's -DWWHD_SWITCH_BASE_REG), instead of an adrp+ldr in every basic block that
 * touches guest memory. Everything this project compiles for the Switch is built with -ffixed-x28,
 * libnx/newlib/deko3d keep x28 (callee-saved), and ppc_enter_guest_thread() sets it where guest
 * threads start (main.c game_thread, sched.c thread_main), which never return. The runtime's own
 * code keeps the explicit load below (its device threads never set x28). */
register uint8_t* ppc_base_x28 __asm__("x28");
#define PPC_MEM_BASE (ppc_base_x28)
#elif defined(__SWITCH__) && defined(__aarch64__) && !defined(__cplusplus)
extern uint8_t* ppc_mem_base_var;
/* Horizon places the window at start-up; the asm read keeps the base in a register across guest
 * stores instead of reloading the global after each one (as upstream's Switch fork). */
static inline __attribute__((always_inline)) uint8_t* ppc_mem_base(void) {
    uint8_t* p;
    __asm__("adrp %0, ppc_mem_base_var\n\tldr %0, [%0, :lo12:ppc_mem_base_var]" : "=r"(p));
    return p;
}
#define PPC_MEM_BASE (ppc_mem_base())
#else
extern uint8_t* ppc_mem_base_var;
#define PPC_MEM_BASE (ppc_mem_base_var)
#endif
#define PPC_PHYS_MASK 0x3FFFFFFFu
#define PPC_IS_MMIO(ea) (((uint32_t)(ea) >> 27) == 0x19u)  /* 0xC8000000-0xCFFFFFFF */

typedef struct Cpu {
    uint32_t r[32];
    uint32_t lr, ctr;
    uint8_t cr[32];         /* one byte per CR bit; bit 4n+0 = crN.lt, +1 gt, +2 eq, +3 so */
    uint8_t xer_so, xer_ov, xer_ca;
    uint8_t xer_bc;
    struct { double ps0, ps1; } f[32];
    uint32_t fpscr;
    uint32_t gqr[8];
    uint32_t res_addr, res_val; /* lwarx/stwcx. reservation */
    uint32_t pc;               /* target for indirect dispatch */
    uint32_t core;             /* host-side: which emulated core this thread runs on */
    uint32_t mod_skip;         /* guest mods: the next entry of this function runs its original code
                                  (fills former padding: sizeof(Cpu) and save states are unchanged) */
    void* thread;              /* host-side: owning guest thread object */
    uint32_t msr;              /* GameCube: this context's MSR (EE = interrupts enabled), cpu.c */
} Cpu;

typedef void (*PpcFunc)(Cpu*);

/* runtime entry points */
void ppc_host_call(Cpu* c, PpcFunc fn);           /* guarded native hook/site entry */
void ppc_dispatch(Cpu* c);                       /* call/jump to c->pc */
void ppc_unimplemented(Cpu* c, uint32_t addr, uint32_t insn);
/* GameCube supervisor state (runtime/src/cpu.c) */
uint32_t ppc_mfmsr(Cpu* c);
void ppc_mtmsr(Cpu* c, uint32_t v);
uint32_t ppc_mfspr(Cpu* c, int spr);
void ppc_mtspr(Cpu* c, int spr, uint32_t v);
void ppc_rfi(Cpu* c, uint32_t addr);
void ppc_sc(Cpu* c, uint32_t addr);
void ppc_trap(Cpu* c, uint32_t addr);
uint64_t ppc_timebase(void);
double ppc_fres(double x);
double ppc_frsqrte(double x);

#define MUSTTAIL __attribute__((musttail))

/* optional guest function-entry trace (runtime switch, see runtime/src/trace.cpp) */
extern int g_ppc_trace;
void ppc_trace_enter(uint32_t addr);
/* GameCube: g_core_preempt[0] is set while an interrupt is pending (sched.c). Checked at every function
   entry and loop back-edge; ppc_preempt delivers it if this context has MSR.EE set. */
extern volatile int g_core_preempt[3];
void ppc_preempt(Cpu* c);
/* the guest function entered last, or a PROF_* tag (runtime.h) while the runtime works: what the
   Switch's sampling profiler reads (SR_PROFILE) */
extern volatile uint32_t g_ppc_cur_fn, g_ppc_cur_lr;  /* and its return address */
#define PPC_ENTER(a) do {                                                     \
        g_ppc_cur_fn = (a);                                                   \
        g_ppc_cur_lr = c->lr;                                                 \
        if (__builtin_expect(g_ppc_trace, 0)) ppc_trace_enter(a);             \
        if (__builtin_expect(g_core_preempt[0], 0)) ppc_preempt(c);           \
    } while (0)

/* guest mods (docs/mod-sdk-v2.md; game code generated with recomp.py --mod-hooks): every function body
   checks its flag byte; a set flag means a mod hooks or replaces it, and ppc_mod_run (c->pc = the
   function) runs the mods' hooks and the replacement or the original. The mod runtime calls the
   original code by setting c->mod_skip first. Code without --mod-hooks emits no check or hook metadata. */
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((visibility("hidden")))
#endif
extern uint8_t* g_mod_hook_flags;
void ppc_mod_run(Cpu* c);
/* Do not mark this branch unlikely: Apple clang 17 can outline a cold hook
   return into an i1-returning helper, invalidating the void musttail call.
   Keep the entry branch ordinary so musttail stays in its original function. */
#define PPC_MOD_HOOK(i, a) do {                                               \
        if (g_mod_hook_flags[i]) {                                           \
            if (c->mod_skip != (a)) { c->pc = (a); MUSTTAIL return ppc_mod_run(c); } \
            c->mod_skip = 0;                                                  \
        }                                                                     \
    } while (0)

/* loop back-edge (every backward branch inside a function): a compiler barrier. Guest memory is
   shared with the other guest threads, but the generated loads are plain loads, and Cpu is
   __restrict, so without it the compiler may load a guest word once before a call-free loop and spin
   on the stale value forever: games busy-wait on locks and flags (`while (*lock == 1);`) that another
   core changes (issue #62: LLVM 16/17, e.g. Apple clang 16 of Xcode 16, turn such a wait into
   `b .`). The barrier only forces guest memory to be read again on the next iteration; the register
   file stays in host registers (it is __restrict and not an operand). */
/* GameCube: spin loops waiting for a flag set by an interrupt callback (DVD completion, retrace) are
   common and call nothing, so pending interrupts are also delivered at loop back-edges. */
#define PPC_LOOP() do {                                                       \
        __asm__ __volatile__("" ::: "memory");                                \
        if (__builtin_expect(g_core_preempt[0], 0)) ppc_preempt(c);           \
    } while (0)

/* the same, in a function that keeps the stack pointer in a local (leaflocal.py): the interrupt
 * handlers run just below the current r1, so the struct must have it */
#define PPC_LOOP_R1(sp) do { \
        __asm__ __volatile__("" ::: "memory"); \
        if (__builtin_expect(g_core_preempt[0], 0)) { c->r[1] = (sp); ppc_preempt(c); } \
    } while (0)

/* ---- memory ---- */
/* device accesses (runtime/src/mmio.c): hardware registers, the GX FIFO pipe, the EFB */
uint32_t mmio_read(uint32_t ea, int size);
void mmio_write(uint32_t ea, uint32_t v, int size);
void mmio_write64(uint32_t ea, uint64_t v);

/* the guest memory accesses: GCC's size heuristics otherwise leave many of them as calls in the
 * large generated functions (on the Switch nearly all of them were) */
#define PPC_ALWAYS_INLINE __attribute__((always_inline))
static inline uint8_t* ppc_ptr(uint32_t ea) { return PPC_MEM_BASE + (ea & PPC_PHYS_MASK); }
#define PPC_MMIO_UNLIKELY(ea) __builtin_expect(PPC_IS_MMIO(ea), 0)
static inline PPC_ALWAYS_INLINE uint8_t ld8(uint32_t ea) {
    if (PPC_MMIO_UNLIKELY(ea)) return (uint8_t)mmio_read(ea, 1);
    return *ppc_ptr(ea);
}
static inline PPC_ALWAYS_INLINE uint16_t ld16(uint32_t ea) {
    if (PPC_MMIO_UNLIKELY(ea)) return (uint16_t)mmio_read(ea, 2);
    uint16_t v; memcpy(&v, ppc_ptr(ea), 2); return __builtin_bswap16(v);
}
static inline PPC_ALWAYS_INLINE uint32_t ld32(uint32_t ea) {
    if (PPC_MMIO_UNLIKELY(ea)) return mmio_read(ea, 4);
    uint32_t v; memcpy(&v, ppc_ptr(ea), 4); return __builtin_bswap32(v);
}
static inline PPC_ALWAYS_INLINE uint64_t ld64(uint32_t ea) {
    if (PPC_MMIO_UNLIKELY(ea)) return ((uint64_t)mmio_read(ea, 4) << 32) | mmio_read(ea + 4, 4);
    uint64_t v; memcpy(&v, ppc_ptr(ea), 8); return __builtin_bswap64(v);
}
static inline PPC_ALWAYS_INLINE void st8(uint32_t ea, uint8_t v) {
    if (PPC_MMIO_UNLIKELY(ea)) { mmio_write(ea, v, 1); return; }
    *ppc_ptr(ea) = v;
}
static inline PPC_ALWAYS_INLINE void st16(uint32_t ea, uint16_t v) {
    if (PPC_MMIO_UNLIKELY(ea)) { mmio_write(ea, v, 2); return; }
    v = __builtin_bswap16(v); memcpy(ppc_ptr(ea), &v, 2);
}
static inline PPC_ALWAYS_INLINE void st32(uint32_t ea, uint32_t v) {
    if (PPC_MMIO_UNLIKELY(ea)) { mmio_write(ea, v, 4); return; }
    v = __builtin_bswap32(v); memcpy(ppc_ptr(ea), &v, 4);
}
static inline PPC_ALWAYS_INLINE void st64(uint32_t ea, uint64_t v) {
    if (PPC_MMIO_UNLIKELY(ea)) { mmio_write64(ea, v); return; }
    v = __builtin_bswap64(v); memcpy(ppc_ptr(ea), &v, 8);
}

/* the same without the device test, for addresses based on r1 (stack) or r13 (small data), which are
 * always RAM (ppc2c.py ram_suffix) */
static inline PPC_ALWAYS_INLINE uint8_t ld8r(uint32_t ea) { return *ppc_ptr(ea); }
static inline PPC_ALWAYS_INLINE uint16_t ld16r(uint32_t ea) { uint16_t v; memcpy(&v, ppc_ptr(ea), 2); return __builtin_bswap16(v); }
static inline PPC_ALWAYS_INLINE uint32_t ld32r(uint32_t ea) { uint32_t v; memcpy(&v, ppc_ptr(ea), 4); return __builtin_bswap32(v); }
static inline PPC_ALWAYS_INLINE uint64_t ld64r(uint32_t ea) { uint64_t v; memcpy(&v, ppc_ptr(ea), 8); return __builtin_bswap64(v); }
static inline PPC_ALWAYS_INLINE void st8r(uint32_t ea, uint8_t v) { *ppc_ptr(ea) = v; }
static inline PPC_ALWAYS_INLINE void st16r(uint32_t ea, uint16_t v) { v = __builtin_bswap16(v); memcpy(ppc_ptr(ea), &v, 2); }
static inline PPC_ALWAYS_INLINE void st32r(uint32_t ea, uint32_t v) { v = __builtin_bswap32(v); memcpy(ppc_ptr(ea), &v, 4); }
static inline PPC_ALWAYS_INLINE void st64r(uint32_t ea, uint64_t v) { v = __builtin_bswap64(v); memcpy(ppc_ptr(ea), &v, 8); }

/* where a guest thread starts (main.c, sched.c): the Switch's generated code finds the base in x28 */
static inline void ppc_enter_guest_thread(void) {
#if defined(__SWITCH__) && defined(__aarch64__) && defined(PPC_FIXED_X28)
    extern uint8_t* ppc_mem_base_var;
    __asm__ __volatile__("mov x28, %0" ::"r"(ppc_mem_base_var));
#endif
}

/* ---- bit casts ---- */
static inline double u64_as_f64(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline uint64_t f64_as_u64(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline float u32_as_f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t f32_as_u32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ---- signalling-NaN counter (local diagnostic, CMake -DWWHD_SNAN_COUNT=ON, logs with WWHD_SNAN_COUNT=1):
 * counts every float load/store where the host conversion would quiet a signalling NaN (f32 -> double on
 * lfs / psq_l, double -> f32 on stfs / psq_st), per call site (snan_count.cpp, captures/snan.log).
 * Not compiled in by default: then these are no-ops and the code is unchanged. */
#ifdef WWHD_SNAN_COUNT
#ifdef __cplusplus
extern "C" {
#endif
extern int wwhd_snan_on;
void wwhd_snan_hit(int kind, uint32_t ea, uint64_t bits);  /* noinline: its return address is the call site */
#ifdef __cplusplus
}
#endif
enum { WWHD_SNAN_LFS = 0, WWHD_SNAN_STFS = 1, WWHD_SNAN_PSQ_L = 2, WWHD_SNAN_PSQ_ST = 3 };
static inline __attribute__((always_inline)) uint32_t snan_chk32(uint32_t b, uint32_t ea, int kind) {
    if (__builtin_expect(wwhd_snan_on, 0) && (b & 0x7FC00000u) == 0x7F800000u && (b & 0x003FFFFFu))
        wwhd_snan_hit(kind, ea, b);
    return b;
}
static inline __attribute__((always_inline)) double snan_chk64(double d, uint32_t ea, int kind) {
    if (__builtin_expect(wwhd_snan_on, 0)) {
        uint64_t b = f64_as_u64(d);
        if ((b & 0x7FF8000000000000ull) == 0x7FF0000000000000ull && (b & 0x0007FFFFFFFFFFFFull))
            wwhd_snan_hit(kind, ea, b);
    }
    return d;
}
#else
#define snan_chk32(b, ea, kind) (b)
#define snan_chk64(d, ea, kind) (d)
#endif

static inline PPC_ALWAYS_INLINE double ldf32(uint32_t ea) { return (double)u32_as_f32(snan_chk32(ld32(ea), ea, 0)); }
static inline PPC_ALWAYS_INLINE double ldf64(uint32_t ea) { return u64_as_f64(ld64(ea)); }
static inline PPC_ALWAYS_INLINE void stf32(uint32_t ea, double d) { st32(ea, f32_as_u32((float)snan_chk64(d, ea, 1))); }
static inline PPC_ALWAYS_INLINE void stf64(uint32_t ea, double d) { st64(ea, f64_as_u64(d)); }
static inline PPC_ALWAYS_INLINE double ldf32r(uint32_t ea) { return (double)u32_as_f32(snan_chk32(ld32r(ea), ea, 0)); }
static inline PPC_ALWAYS_INLINE double ldf64r(uint32_t ea) { return u64_as_f64(ld64r(ea)); }
static inline PPC_ALWAYS_INLINE void stf32r(uint32_t ea, double d) { st32r(ea, f32_as_u32((float)snan_chk64(d, ea, 1))); }
static inline PPC_ALWAYS_INLINE void stf64r(uint32_t ea, double d) { st64r(ea, f64_as_u64(d)); }

/* ---- integer helpers ---- */
static inline uint32_t rotl32(uint32_t v, uint32_t sh) { sh &= 31; return sh ? (v << sh) | (v >> (32 - sh)) : v; }

static inline void cr_set_s(Cpu* c, int f, int32_t a, int32_t b) {
    c->cr[4 * f + 0] = a < b; c->cr[4 * f + 1] = a > b; c->cr[4 * f + 2] = a == b; c->cr[4 * f + 3] = c->xer_so;
}
static inline void cr_set_u(Cpu* c, int f, uint32_t a, uint32_t b) {
    c->cr[4 * f + 0] = a < b; c->cr[4 * f + 1] = a > b; c->cr[4 * f + 2] = a == b; c->cr[4 * f + 3] = c->xer_so;
}
static inline void cr0_rc(Cpu* c, uint32_t v) { cr_set_s(c, 0, (int32_t)v, 0); }
/* The recompiler's condition-register liveness pass (tools/recomp/crlive.py) stores only the bits
 * that are read later: m has bit 0 = lt, 1 = gt, 2 = eq, 3 = so of the field. With PPC_CR_CHECK, the
 * dropped bits get a poison value that ppc_cr_read() refuses (a check build of the recompiler). */
#ifdef PPC_CR_CHECK
#define PPC_CR_DEAD(c, i) ((c)->cr[i] = 0x55)
void ppc_cr_poisoned(Cpu* c, int bit, uint32_t addr);
static inline uint8_t ppc_cr_read(Cpu* c, int bit, uint32_t addr) {
    if (__builtin_expect(c->cr[bit] == 0x55, 0)) ppc_cr_poisoned(c, bit, addr);
    return c->cr[bit];
}
#else
#define PPC_CR_DEAD(c, i) ((void)0)
#endif
static inline __attribute__((always_inline)) void cr_set_s_m(Cpu* c, int f, int32_t a, int32_t b, int m) {
    if (m & 1) c->cr[4 * f + 0] = a < b; else PPC_CR_DEAD(c, 4 * f + 0);
    if (m & 2) c->cr[4 * f + 1] = a > b; else PPC_CR_DEAD(c, 4 * f + 1);
    if (m & 4) c->cr[4 * f + 2] = a == b; else PPC_CR_DEAD(c, 4 * f + 2);
    if (m & 8) c->cr[4 * f + 3] = c->xer_so; else PPC_CR_DEAD(c, 4 * f + 3);
}
static inline __attribute__((always_inline)) void cr_set_u_m(Cpu* c, int f, uint32_t a, uint32_t b, int m) {
    if (m & 1) c->cr[4 * f + 0] = a < b; else PPC_CR_DEAD(c, 4 * f + 0);
    if (m & 2) c->cr[4 * f + 1] = a > b; else PPC_CR_DEAD(c, 4 * f + 1);
    if (m & 4) c->cr[4 * f + 2] = a == b; else PPC_CR_DEAD(c, 4 * f + 2);
    if (m & 8) c->cr[4 * f + 3] = c->xer_so; else PPC_CR_DEAD(c, 4 * f + 3);
}
static inline __attribute__((always_inline)) void cr0_rc_m(Cpu* c, uint32_t v, int m) { cr_set_s_m(c, 0, (int32_t)v, 0, m); }

static inline uint32_t ppc_divw(uint32_t a, uint32_t b) {
    if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) return ((int32_t)a < 0) ? 0xFFFFFFFFu : 0;
    return (uint32_t)((int32_t)a / (int32_t)b);
}
static inline uint32_t ppc_divwu(uint32_t a, uint32_t b) { return b ? a / b : 0; }

/* eight CR bytes (0/1, little-endian host) to eight bits, the first byte in the top bit */
static inline uint32_t ppc_cr_pack8(const uint8_t* p) {
    uint64_t x;
    memcpy(&x, p, 8);
    return (uint32_t)(((x & 0x0101010101010101ull) * 0x8040201008040201ull) >> 56);
}
static inline __attribute__((always_inline)) uint32_t ppc_mfcr(const Cpu* c) {
    return ppc_cr_pack8(c->cr) << 24 | ppc_cr_pack8(c->cr + 8) << 16 | ppc_cr_pack8(c->cr + 16) << 8 | ppc_cr_pack8(c->cr + 24);
}
static inline void ppc_mtcrf(Cpu* c, uint32_t crm, uint32_t v) {
    for (int f = 0; f < 8; f++)
        if (crm & (0x80u >> f))
            for (int b = 0; b < 4; b++) c->cr[4 * f + b] = (v >> (31 - (4 * f + b))) & 1;
}
static inline uint32_t ppc_mfxer(const Cpu* c) {
    return ((uint32_t)c->xer_so << 31) | ((uint32_t)c->xer_ov << 30) | ((uint32_t)c->xer_ca << 29) | c->xer_bc;
}
static inline void ppc_mtxer(Cpu* c, uint32_t v) {
    c->xer_so = (v >> 31) & 1; c->xer_ov = (v >> 30) & 1; c->xer_ca = (v >> 29) & 1; c->xer_bc = v & 0x7F;
}

/* lwarx / stwcx. */
static inline uint32_t ppc_lwarx(Cpu* c, uint32_t ea) {
    uint32_t raw = __atomic_load_n((uint32_t*)ppc_ptr(ea), __ATOMIC_SEQ_CST);
    c->res_addr = ea; c->res_val = raw;
    return __builtin_bswap32(raw);
}
static inline void ppc_stwcx(Cpu* c, uint32_t ea, uint32_t v) {
    int ok = 0;
    if (c->res_addr == ea) {
        uint32_t expected = c->res_val;
        ok = __atomic_compare_exchange_n((uint32_t*)ppc_ptr(ea), &expected, __builtin_bswap32(v), 0,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    c->res_addr = 0xFFFFFFFFu;
    c->cr[0] = 0; c->cr[1] = 0; c->cr[2] = (uint8_t)ok; c->cr[3] = c->xer_so;
}

static inline void ppc_dcbz(uint32_t ea) { memset(ppc_ptr(ea & ~31u), 0, 32); }

/* ---- floating point ---- */
static inline double round25(double d) {
    uint64_t v = f64_as_u64(d);
    v = (v & 0xFFFFFFFFF8000000ull) + (v & 0x8000000ull);
    return u64_as_f64(v);
}
static inline double to_single(double d) { return (double)(float)d; }
/* checking build of the single-precision tracking (RECOMP_SINGLE_CHECK=1, tools/recomp/singleflow.py):
 * an operand the recompiler left without round25 must be single precision already */
void ppc_single_report(uint32_t addr, double v);
static inline double ppc_single_chk(double v, uint32_t addr) {
    if (__builtin_expect(f64_as_u64(round25(v)) != f64_as_u64(v), 0)) ppc_single_report(addr, v);
    return v;
}

/* checking build of the register summaries (RECOMP_SUMMARY_CHECK=1, tools/recomp/leaflocal.py): around a
 * call that syncs only the callee's summary, every other register (but r2/r13, the small-data bases
 * that interrupt handlers use) gets garbage in the struct, and is checked and put back after the call.
 * A callee that read one would misbehave visibly; one that wrote one is reported. */
typedef struct { uint32_t r[32]; double f[32][2]; } PpcSumSave;
void ppc_sum_report(uint32_t site, int reg);
static inline void ppc_sum_poison(Cpu* c, uint64_t keep, PpcSumSave* s) {
    keep |= (1ull << 2) | (1ull << 13);
    for (int i = 0; i < 32; i++)
        if (!(keep >> i & 1)) { s->r[i] = c->r[i]; c->r[i] = 0xDEAD0000u | (uint32_t)i; }
    for (int i = 0; i < 32; i++)
        if (!(keep >> (32 + i) & 1)) {
            s->f[i][0] = c->f[i].ps0; s->f[i][1] = c->f[i].ps1;
            c->f[i].ps0 = c->f[i].ps1 = u64_as_f64(0x7FF4DEAD00000000ull | (uint64_t)i);
        }
}
static inline void ppc_sum_check(Cpu* c, uint64_t keep, PpcSumSave* s, uint32_t site) {
    keep |= (1ull << 2) | (1ull << 13);
    for (int i = 0; i < 32; i++)
        if (!(keep >> i & 1)) {
            if (c->r[i] != (0xDEAD0000u | (uint32_t)i)) ppc_sum_report(site, i);
            c->r[i] = s->r[i];
        }
    for (int i = 0; i < 32; i++)
        if (!(keep >> (32 + i) & 1)) {
            if (f64_as_u64(c->f[i].ps0) != (0x7FF4DEAD00000000ull | (uint64_t)i) ||
                f64_as_u64(c->f[i].ps1) != (0x7FF4DEAD00000000ull | (uint64_t)i))
                ppc_sum_report(site, 32 + i);
            c->f[i].ps0 = s->f[i][0]; c->f[i].ps1 = s->f[i][1];
        }
}

/* checking build of the float arithmetic (RECOMP_FLOAT_CHECK=1, tools/recomp/singleflow.py): the
 * double-then-single result and the single-precision one must be the same bits (NaNs: both NaN) */
void ppc_float_report(uint32_t addr, double ref, double got);
static inline double ppc_float_chk(double ref, double got, uint32_t addr) {
    if (__builtin_expect(f64_as_u64(ref) != f64_as_u64(got) && !(ref != ref && got != got), 0)) ppc_float_report(addr, ref, got);
    return ref;
}

/* fcmpu / fcmpo. IEEE comparisons with a NaN are false, so lt/gt/eq need no NaN test; un is the
 * fourth outcome. FPSCR's FPCC field (which fcmp also sets) is not kept: only mffs and mcrfs read
 * it, and this game has neither (ppc2c warns when it translates one). */
static inline void cr_set_f(Cpu* c, int f, double a, double b) {
    c->cr[4 * f + 0] = a < b; c->cr[4 * f + 1] = a > b;
    c->cr[4 * f + 2] = a == b; c->cr[4 * f + 3] = (uint8_t)__builtin_isunordered(a, b);
}
/* cr_set_f storing only the bits in m (see cr_set_s_m) */
static inline __attribute__((always_inline)) void cr_set_f_m(Cpu* c, int f, double a, double b, int m) {
    if (m & 1) c->cr[4 * f + 0] = a < b; else PPC_CR_DEAD(c, 4 * f + 0);
    if (m & 2) c->cr[4 * f + 1] = a > b; else PPC_CR_DEAD(c, 4 * f + 1);
    if (m & 4) c->cr[4 * f + 2] = a == b; else PPC_CR_DEAD(c, 4 * f + 2);
    if (m & 8) c->cr[4 * f + 3] = (uint8_t)__builtin_isunordered(a, b); else PPC_CR_DEAD(c, 4 * f + 3);
}

/* The same compares for leaf functions that keep CR bits in C locals (tools/recomp/leaflocal.py):
 * lt, gt, eq, so point at the field's four locals. */
#ifdef PPC_CR_CHECK
#define PPC_CRL_DEAD(p) (*(p) = 0x55)
static inline uint8_t ppc_crl_read(Cpu* c, uint8_t v, int bit, uint32_t addr) {
    if (__builtin_expect(v == 0x55, 0)) ppc_cr_poisoned(c, bit, addr);
    return v;
}
#else
#define PPC_CRL_DEAD(p) ((void)0)
#endif
static inline __attribute__((always_inline)) void crl_set_s_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                              const Cpu* c, int32_t a, int32_t b, int m) {
    if (m & 1) *lt = a < b; else PPC_CRL_DEAD(lt);
    if (m & 2) *gt = a > b; else PPC_CRL_DEAD(gt);
    if (m & 4) *eq = a == b; else PPC_CRL_DEAD(eq);
    if (m & 8) *so = c->xer_so; else PPC_CRL_DEAD(so);
}
static inline __attribute__((always_inline)) void crl_set_u_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                              const Cpu* c, uint32_t a, uint32_t b, int m) {
    if (m & 1) *lt = a < b; else PPC_CRL_DEAD(lt);
    if (m & 2) *gt = a > b; else PPC_CRL_DEAD(gt);
    if (m & 4) *eq = a == b; else PPC_CRL_DEAD(eq);
    if (m & 8) *so = c->xer_so; else PPC_CRL_DEAD(so);
}
static inline __attribute__((always_inline)) void crl_set_f_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                              const Cpu* c, double a, double b, int m) {
    (void)c;
    if (m & 1) *lt = a < b; else PPC_CRL_DEAD(lt);
    if (m & 2) *gt = a > b; else PPC_CRL_DEAD(gt);
    if (m & 4) *eq = a == b; else PPC_CRL_DEAD(eq);
    if (m & 8) *so = (uint8_t)__builtin_isunordered(a, b); else PPC_CRL_DEAD(so);
}
static inline __attribute__((always_inline)) void crl0_rc_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                            const Cpu* c, uint32_t v, int m) {
    crl_set_s_m(lt, gt, eq, so, c, (int32_t)v, 0, m);
}
#define crl_set_s(lt, gt, eq, so, c, a, b) crl_set_s_m(lt, gt, eq, so, c, a, b, 15)
#define crl_set_u(lt, gt, eq, so, c, a, b) crl_set_u_m(lt, gt, eq, so, c, a, b, 15)
#define crl_set_f(lt, gt, eq, so, c, a, b) crl_set_f_m(lt, gt, eq, so, c, a, b, 15)
#define crl0_rc(lt, gt, eq, so, c, v) crl0_rc_m(lt, gt, eq, so, c, v, 15)

static inline uint64_t ppc_fctiwz(double d) {
    int32_t r;
    if (isnan(d)) r = (int32_t)0x80000000;
    else if (d >= 2147483647.0) r = 0x7FFFFFFF;
    else if (d <= -2147483648.0) r = (int32_t)0x80000000;
    else r = (int32_t)d;
    return 0xFFF8000000000000ull | (uint32_t)r;
}
static inline uint64_t ppc_fctiw(Cpu* c, double d) {
    switch (c->fpscr & 3) {
    case 0: d = nearbyint(d); break; /* default host mode is round-to-nearest-even */
    case 1: d = trunc(d); break;
    case 2: d = ceil(d); break;
    case 3: d = floor(d); break;
    }
    return ppc_fctiwz(d);
}
static inline double ppc_fsel(double a, double b, double cc) { return a >= 0.0 ? cc : b; }

/* ---- paired single quantization ---- */
/* 2^e for the 6-bit signed GQR scale, built from float bits (no libm call) */
static inline float psq_pow2(int e) { return u32_as_f32((uint32_t)(127 + e) << 23); }
static inline float psq_dequant(uint32_t data, uint32_t type, uint32_t scale) {
    if (type < 4) return u32_as_f32(data);  /* float: no scaling */
    float s = psq_pow2(-(int)((int32_t)(scale << 26) >> 26));
    switch (type) {
    case 4: return (float)(uint8_t)data * s;
    case 5: return (float)(uint16_t)data * s;
    case 6: return (float)(int8_t)data * s;
    case 7: return (float)(int16_t)data * s;
    default: return u32_as_f32(data);
    }
}
static inline uint32_t psq_quant(float v, uint32_t type, uint32_t scale) {
    if (type < 4) return f32_as_u32(v);
    float s = psq_pow2((int)((int32_t)(scale << 26) >> 26));
    switch (type) {
    case 4: v *= s; v = v < 0 ? 0 : v > 255 ? 255 : v; return (uint8_t)(uint32_t)v;
    case 5: v *= s; v = v < 0 ? 0 : v > 65535 ? 65535 : v; return (uint16_t)(uint32_t)v;
    case 6: v *= s; v = v < -128 ? -128 : v > 127 ? 127 : v; return (uint8_t)(int32_t)v;
    case 7: v *= s; v = v < -32768 ? -32768 : v > 32767 ? 32767 : v; return (uint16_t)(int32_t)v;
    default: return f32_as_u32(v);
    }
}
/* quantized formats (GQR type 4-7): out of line, so the float case below stays small and inline.
 * The _l forms take the two halves of the FPR separately (for code that keeps registers in C locals). */
static __attribute__((noinline)) void psq_load_slow_l(Cpu* c, double* p0, double* p1, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = (g >> 16) & 7, scale = (g >> 24) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = sz == 1 ? ld8(ea) : sz == 2 ? ld16(ea) : snan_chk32(ld32(ea), ea, 2);
    *p0 = psq_dequant(d0, type, scale);
    if (w) *p1 = 1.0;
    else {
        uint32_t d1 = sz == 1 ? ld8(ea + 1) : sz == 2 ? ld16(ea + 2) : snan_chk32(ld32(ea + 4), ea + 4, 2);
        *p1 = psq_dequant(d1, type, scale);
    }
}
static __attribute__((noinline)) void psq_store_slow_l(Cpu* c, double v0, double v1, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = g & 7, scale = (g >> 8) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = psq_quant((float)(sz == 4 ? snan_chk64(v0, ea, 3) : v0), type, scale);
    if (sz == 1) st8(ea, d0); else if (sz == 2) st16(ea, d0); else st32(ea, d0);
    if (!w) {
        uint32_t d1 = psq_quant((float)(sz == 4 ? snan_chk64(v1, ea + 4, 3) : v1), type, scale);
        if (sz == 1) st8(ea + 1, d1); else if (sz == 2) st16(ea + 2, d1); else st32(ea + 4, d1);
    }
}
/* paired-single loads and stores: plain floats (GQR type 0-3) are nearly all of them. The generated
 * code (funcs.h) sets bit n of PPC_GQR_STATIC_FLOAT when the game never writes GQRn, which then stays
 * 0 (plain floats): with the constant GQR index of every call the check disappears (2,082 of the
 * game's 2,106 paired loads and stores use GQR0 or GQR1). */
#ifndef PPC_GQR_STATIC_FLOAT
#define PPC_GQR_STATIC_FLOAT 0
#endif
/* quantized formats in RAM, inline: only the ~24 sites whose GQR the game writes reach these, and
 * Sofdec's video decoder (the intro FMV) runs them for every pixel. One device test per pair, the
 * element size known from the type; device addresses go to the generic code. Same values as
 * psq_dequant / psq_quant. */
static inline __attribute__((always_inline)) int psq_load_q(double* p0, double* p1, uint32_t ea, int w, uint32_t g) {
    uint32_t type = (g >> 16) & 7;
    if (__builtin_expect(PPC_IS_MMIO(ea), 0)) return 0;
    float s = psq_pow2(-((int32_t)(g << 2) >> 26));  /* scale: GQR bits 24-29, signed */
    const uint8_t* p = ppc_ptr(ea);
    uint16_t h0, h1;
    switch (type) {
    case 4: *p0 = (float)p[0] * s; if (w) *p1 = 1.0; else *p1 = (float)p[1] * s; return 1;
    case 6: *p0 = (float)(int8_t)p[0] * s; if (w) *p1 = 1.0; else *p1 = (float)(int8_t)p[1] * s; return 1;
    case 5:
    case 7:
        memcpy(&h0, p, 2);
        h0 = __builtin_bswap16(h0);
        *p0 = (type == 5 ? (float)h0 : (float)(int16_t)h0) * s;
        if (w) *p1 = 1.0;
        else {
            memcpy(&h1, p + 2, 2);
            h1 = __builtin_bswap16(h1);
            *p1 = (type == 5 ? (float)h1 : (float)(int16_t)h1) * s;
        }
        return 1;
    }
    return 0;
}
static inline __attribute__((always_inline)) int psq_store_q(double v0, double v1, uint32_t ea, int w, uint32_t g) {
    uint32_t type = g & 7;
    if (__builtin_expect(PPC_IS_MMIO(ea) || type < 4, 0)) return 0;
    uint32_t scale = (g >> 8) & 0x3F;
    uint8_t* p = ppc_ptr(ea);
    uint32_t d0 = psq_quant((float)v0, type, scale);
    if (type == 4 || type == 6) {
        p[0] = (uint8_t)d0;
        if (!w) p[1] = (uint8_t)psq_quant((float)v1, type, scale);
    } else {
        uint16_t h = __builtin_bswap16((uint16_t)d0);
        memcpy(p, &h, 2);
        if (!w) {
            h = __builtin_bswap16((uint16_t)psq_quant((float)v1, type, scale));
            memcpy(p + 2, &h, 2);
        }
    }
    return 1;
}
static inline __attribute__((always_inline)) void psq_load_l(Cpu* c, double* p0, double* p1, uint32_t ea, int w, int i) {
    if (((PPC_GQR_STATIC_FLOAT >> i) & 1) || __builtin_expect(((c->gqr[i] >> 16) & 7) < 4, 1)) {
        *p0 = u32_as_f32(snan_chk32(ld32(ea), ea, 2));
        *p1 = w ? 1.0 : (double)u32_as_f32(snan_chk32(ld32(ea + 4), ea + 4, 2));
        return;
    }
    if (psq_load_q(p0, p1, ea, w, c->gqr[i])) return;
    psq_load_slow_l(c, p0, p1, ea, w, i);
}
static inline __attribute__((always_inline)) void psq_store_l(Cpu* c, double v0, double v1, uint32_t ea, int w, int i) {
    if (((PPC_GQR_STATIC_FLOAT >> i) & 1) || __builtin_expect((c->gqr[i] & 7) < 4, 1)) {
        st32(ea, f32_as_u32((float)snan_chk64(v0, ea, 3)));
        if (!w) st32(ea + 4, f32_as_u32((float)snan_chk64(v1, ea + 4, 3)));
        return;
    }
    if (psq_store_q(v0, v1, ea, w, c->gqr[i])) return;
    psq_store_slow_l(c, v0, v1, ea, w, i);
}
static inline __attribute__((always_inline)) void psq_load(Cpu* c, int fd, uint32_t ea, int w, int i) {
    psq_load_l(c, &c->f[fd].ps0, &c->f[fd].ps1, ea, w, i);
}
static inline __attribute__((always_inline)) void psq_store(Cpu* c, int fs, uint32_t ea, int w, int i) {
    psq_store_l(c, c->f[fs].ps0, c->f[fs].ps1, ea, w, i);
}

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) && !defined(_WIN32)
#pragma GCC visibility pop
#endif
