/* DSP block (0xCC005000) and Audio Interface (0xCC006C00):
 *
 *  - DSP control/status register, the two mailboxes and the DSP's interrupt.
 *  - DSP "HLE": the ROM's boot handshake (0x8071FEED, then the task's boot parameters) and a stand-in
 *    for the AX microcode: it takes each command list (0xBABE0000|size, address), processes it
 *    (ax.c) and yields (DSP_YIELD), as the real microcode does at the end of a frame. The lists are
 *    mixed on a worker thread, in parallel with the game as on the real DSP (the game learns the
 *    frame is done from the DSP interrupt); SR_DSP_SYNC=1 mixes on the game thread instead.
 *  - ARAM (16 MiB host buffer) and its DMA engine, raising the ARAM interrupt when done.
 *  - AI DMA (registers in the DSP block): while enabled, a host thread "plays" the buffer and raises
 *    the AI-DMA interrupt every block (5 ms frames at 32 kHz for AX), which drives AX's frame loop.
 * Protocol details: the game's DSP library (__DSP_boot_task, __DSPHandler) and Dolphin's DSPHLE. */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "runtime.h"

#define OS_INTERRUPT_DSP_AI   5
#define OS_INTERRUPT_DSP_ARAM 6
#define OS_INTERRUPT_DSP_DSP  7

/* CSR bits */
#define CSR_RES       0x0001
#define CSR_HALT      0x0004
#define CSR_AIDINT    0x0008
#define CSR_AIDMSK    0x0010
#define CSR_ARINT     0x0020
#define CSR_ARMSK     0x0040
#define CSR_DSPINT    0x0080
#define CSR_DSPMSK    0x0100
#define CSR_INTS      (CSR_AIDINT | CSR_ARINT | CSR_DSPINT)

#define ARAM_SIZE 0x01000000u

static mtx_t g_lock;
static volatile uint16_t g_csr;
static uint16_t g_regs[0x40];          /* 0x00-0x7F, 16-bit */
static uint8_t* g_aram;

/* mailboxes */
static uint32_t g_to_dsp_hi;
#define OUTQ 64
static struct { uint32_t mail; int irq; } g_out[OUTQ];
static unsigned g_out_head, g_out_tail;

/* DSP state */
enum { DSP_OFF, DSP_ROM, DSP_AX };
static int g_state = DSP_OFF;
static int g_boot_step;
static uint32_t g_boot[10];
static int g_cmdlist_next;
static uint32_t g_cmdlist_size;

static void raise_dspint(void) {
    g_csr |= CSR_DSPINT;
    if (g_csr & CSR_DSPMSK) os_raise_interrupt(OS_INTERRUPT_DSP_DSP);
}

static void push_mail(uint32_t mail, int irq) {
    if (g_out_tail - g_out_head >= OUTQ) rt_fatal("DSP outgoing mailbox overflow");
    g_out[g_out_tail % OUTQ].mail = mail;
    g_out[g_out_tail % OUTQ].irq = irq;
    if (g_out_tail++ == g_out_head && irq) raise_dspint();
}

/* the CPU took the front mail: the next one (if any) becomes visible */
static void pop_mail(void) {
    if (g_out_head == g_out_tail) return;
    g_out_head++;
    if (g_out_head != g_out_tail && g_out[g_out_head % OUTQ].irq) raise_dspint();
}

#define DSP_INIT  0xDCD10000u
#define DSP_YIELD 0xDCD10002u

/* ---- the mixing worker: the game thread queues each command list and goes on; the worker mixes it
 * (ax.c) and then yields. Mixed on the game thread (inside its AI interrupt, ~3.3 times a frame),
 * the lists cost the game core 5-7% of a 3-4 player race on the Switch, where the slow parts miss
 * the 16.7 ms frame by about that much. ---- */
static mtx_t g_ax_lock;
static cnd_t g_ax_cv;
#define AXQ 8
static struct { uint32_t addr, size; unsigned gen; uint64_t t; } g_axq[AXQ];
static unsigned g_axq_head, g_axq_tail;  /* taken / queued, ever (g_ax_lock) */
static unsigned g_dsp_gen;               /* bumped by a DSP reset: a list queued before it does not yield */
static int g_ax_sync;                    /* SR_DSP_SYNC=1 */
static _Atomic uint64_t g_ax_ticks;      /* time spent mixing, for [perf] */

void dsp_ax_stats(uint64_t* ticks) { *ticks = atomic_exchange(&g_ax_ticks, 0); }
/* command lists the worker started more than one 5 ms audio frame after the game sent them (the game
 * then drops audio frames: crackling), and the longest such wait, since the last call */
static _Atomic unsigned g_ax_late;
static _Atomic uint64_t g_ax_wait_max;
void dsp_ax_late_stats(unsigned* late, uint64_t* wait_max) {
    *late = atomic_exchange(&g_ax_late, 0);
    *wait_max = atomic_exchange(&g_ax_wait_max, 0);
}

static void ax_mix(uint32_t addr, uint32_t size) {
    uint64_t t = rt_ticks();
    ax_process_command_list(addr, size);
    atomic_fetch_add(&g_ax_ticks, rt_ticks() - t);
}

static int ax_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);  /* the Switch's render-thread core (the game has core 0, the GP thread core 2) */
    /* above the render thread: Horizon does not time-slice equal priorities, and a render frame (30+ ms
     * at stock clocks) kept mixes waiting past their 5 ms deadline (hardware test 27: ~10% of audio
     * frames dropped in races, crackling) */
    rt_thread_raise_priority();
    for (;;) {
        mtx_lock(&g_ax_lock);
        while (g_axq_head == g_axq_tail) cnd_wait(&g_ax_cv, &g_ax_lock);
        uint32_t addr = g_axq[g_axq_head % AXQ].addr, size = g_axq[g_axq_head % AXQ].size;
        unsigned gen = g_axq[g_axq_head % AXQ].gen;
        uint64_t wait = rt_ticks() - g_axq[g_axq_head % AXQ].t;
        mtx_unlock(&g_ax_lock);
        if (wait > 40500000ull / 200) atomic_fetch_add(&g_ax_late, 1);
        if (wait > atomic_load(&g_ax_wait_max)) atomic_store(&g_ax_wait_max, wait);
        ax_mix(addr, size);
        mtx_lock(&g_ax_lock);
        g_axq_head++;
        cnd_broadcast(&g_ax_cv);
        mtx_unlock(&g_ax_lock);
        mtx_lock(&g_lock);
        if (gen == g_dsp_gen && g_state == DSP_AX) push_mail(DSP_YIELD, 1);
        mtx_unlock(&g_lock);
    }
    return 0;
}

/* called with g_lock held */
static void ax_queue(uint32_t addr, uint32_t size) {
    if (g_ax_sync) {
        ax_mix(addr, size);
        push_mail(DSP_YIELD, 1);
        return;
    }
    mtx_lock(&g_ax_lock);  /* the worker never waits for g_lock while holding g_ax_lock */
    while (g_axq_tail - g_axq_head >= AXQ) cnd_wait(&g_ax_cv, &g_ax_lock);
    g_axq[g_axq_tail % AXQ].addr = addr;
    g_axq[g_axq_tail % AXQ].size = size;
    g_axq[g_axq_tail % AXQ].gen = g_dsp_gen;
    g_axq[g_axq_tail % AXQ].t = rt_ticks();
    g_axq_tail++;
    cnd_broadcast(&g_ax_cv);
    mtx_unlock(&g_ax_lock);
}

static void dsp_mail_from_cpu(uint32_t mail) {
    if (g_state == DSP_ROM) {  /* task boot parameters: 5 (command, value) pairs */
        g_boot[g_boot_step++] = mail;
        if (g_boot_step == 10) {
            g_boot_step = 0;
            uint32_t iram_mm = g_boot[1], iram_len = g_boot[5];
            rt_log("[dsp] task boot: IRAM image %08X (%u bytes), entry %04X; using the AX stand-in", iram_mm, iram_len,
                   g_boot[9]);
            g_state = DSP_AX;
            g_cmdlist_next = 0;
            push_mail(DSP_INIT, 1);
        }
        return;
    }
    if (g_state != DSP_AX) return;
    if (g_cmdlist_next) {
        g_cmdlist_next = 0;
        ax_queue(mail, g_cmdlist_size);
        return;
    }
    if ((mail & 0xFFFF0000u) == 0xBABE0000u) {
        g_cmdlist_next = 1;
        g_cmdlist_size = mail & 0xFFFF;
        return;
    }
    switch (mail) {
    case 0xCDD10003u: break;   /* continue: the CPU sends the next command list directly */
    case 0xCDD10001u:          /* switch to another task: only AX runs here */
    case 0xCDD10002u:          /* reset */
    default:
        rt_log("[dsp] unhandled mail %08X", mail);
        break;
    }
}

/* ---- AI DMA: the audio clock ---- */
static atomic_uint g_ai_dma_on;
static uint32_t g_ai_start, g_ai_blocks;

/* SR_DETCLOCK (online play groundwork): the audio clock follows the game. Each game frame gets the
 * DMA interrupts that 1/60 s of 32 kHz audio takes (ai_det_frame, at the frame end; the fraction is
 * carried over), handed over one at a time while the main thread sleeps in its frame wait and the CPU
 * is otherwise idle (ai_det_try_raise, from vi.c), before that frame's retrace. */
static uint32_t g_ai_det_bytes;     /* the current DMA block size (ai_thread) */
static _Atomic int g_ai_det_tokens;
static uint64_t g_ai_det_acc;       /* bytes of audio time owed, x60 */
void ai_det_frame(void) {
    uint32_t bytes = g_ai_det_bytes;
    if (!atomic_load(&g_ai_dma_on) || !bytes) return;
    g_ai_det_acc += 128000;  /* 128000 bytes/s at 32 kHz 16-bit stereo; per frame = 128000/60 */
    int n = (int)(g_ai_det_acc / ((uint64_t)bytes * 60));
    g_ai_det_acc -= (uint64_t)n * bytes * 60;
    int t = atomic_fetch_add(&g_ai_det_tokens, n) + n;
    if (t > 8) atomic_store(&g_ai_det_tokens, 8);
}
int ai_det_pending(void) { return atomic_load(&g_ai_det_tokens) > 0; }
int ai_det_try_raise(void) {  /* 1: one was raised or is being handled (the retrace waits) */
    if (atomic_load(&g_ai_det_tokens) <= 0) return 0;
    mtx_lock(&g_lock);
    if (!(g_csr & CSR_AIDMSK)) {  /* masked: nothing would run; the tokens lapse */
        atomic_store(&g_ai_det_tokens, 0);
        mtx_unlock(&g_lock);
        return 0;
    }
    if (g_csr & CSR_AIDINT) { mtx_unlock(&g_lock); return 1; }  /* the previous one is not handled yet: wait */
    atomic_fetch_sub(&g_ai_det_tokens, 1);
    g_csr |= CSR_AIDINT;
    mtx_unlock(&g_lock);
    os_raise_interrupt(OS_INTERRUPT_DSP_AI);
    return 1;
}

static int ai_thread(void* arg) {
    (void)arg;
    rt_thread_pin(2);
    uint64_t next = rt_ticks();
    for (;;) {
        if (!atomic_load(&g_ai_dma_on)) {
            platform_sleep_ns(1000000);
            next = rt_ticks();
            continue;
        }
        uint32_t bytes = g_ai_blocks * 32;
        /* 32 kHz, 16-bit stereo: 128000 bytes/s = 40500000 ticks per 128000 bytes */
        next += (uint64_t)bytes * 40500000 / 128000;
        if ((int64_t)(rt_ticks() - next) > 40500000 / 2) {  /* half a second behind (sleep): resync */
            rt_log("[dsp] audio clock %.1f s behind (sleep or suspension): resynchronized",
                   (double)(rt_ticks() - next) / 40500000.0);
            next = rt_ticks();
        }
        for (;;) {
            int64_t left = (int64_t)(next - rt_ticks());
            if (left <= 0) break;
            platform_sleep_ns((uint64_t)left * 2000 / 81);
        }
        audio_submit(g_ai_start, bytes);
        {   /* SR_DETCLOCK: the interrupt is handed over per game frame (ai_det_try_raise) instead */
            int ppc_detclock(void); uint32_t ppc_detclock_steps(void);
            if (ppc_detclock() && ppc_detclock_steps() > 0) { g_ai_det_bytes = bytes; continue; }
        }
        mtx_lock(&g_lock);
        g_csr |= CSR_AIDINT;
        int irq = (g_csr & CSR_AIDMSK) != 0;
        mtx_unlock(&g_lock);
        if (irq) os_raise_interrupt(OS_INTERRUPT_DSP_AI);
    }
    return 0;
}

/* ---- ARAM DMA ---- */
static uint64_t g_aram_bytes, g_aram_ticks;  /* for [perf] (game thread only) */
void dsp_aram_stats(uint64_t* bytes, uint64_t* ticks) {
    *bytes = g_aram_bytes; *ticks = g_aram_ticks;
    g_aram_bytes = g_aram_ticks = 0;
}
static void aram_dma(void) {
    uint32_t mm = ((uint32_t)g_regs[0x20 >> 1] << 16 | g_regs[0x22 >> 1]) & 0x03FFFFE0;
    uint32_t ar = ((uint32_t)g_regs[0x24 >> 1] << 16 | g_regs[0x26 >> 1]) & 0x03FFFFE0;
    uint32_t cnt = (uint32_t)g_regs[0x28 >> 1] << 16 | g_regs[0x2A >> 1];
    uint32_t len = cnt & 0x7FFFFFE0;
    int to_mm = (cnt >> 31) & 1;
    if (mm + len > GUEST_RAM_SIZE) rt_fatal("ARAM DMA beyond main RAM (%08X+%X)", mm, len);
    static int log_dma = -1;  /* SR_DVDLOG=1 also logs ARAM transfers */
    if (log_dma < 0) log_dma = getenv("SR_DVDLOG") != NULL;
    if (log_dma) rt_log("[aramlog] %s %X bytes: RAM %08X, ARAM %07X (field %u)", to_mm ? "to RAM" : "to ARAM", len,
                        mm | 0x80000000u, ar, vi_field_count());
    uint64_t t0 = rt_ticks();
    for (uint32_t done = 0; done < len;) {  /* ARAM addresses wrap at its size */
        uint32_t a = (ar + done) % ARAM_SIZE, n = len - done;
        if (n > ARAM_SIZE - a) n = ARAM_SIZE - a;
        if (to_mm) memcpy(mem_phys(mm + done), g_aram + a, n);
        else memcpy(g_aram + a, mem_phys(mm + done), n);
        done += n;
    }
    g_aram_bytes += len;
    g_aram_ticks += rt_ticks() - t0;
    g_regs[0x28 >> 1] = 0;
    g_regs[0x2A >> 1] = 0;
    g_csr |= CSR_ARINT;
    if (g_csr & CSR_ARMSK) os_raise_interrupt(OS_INTERRUPT_DSP_ARAM);
    else {
        static int logged;
        if (logged++ < 10)
            rt_log("[dsp] ARAM DMA (%s %X bytes) completed with its interrupt masked: raised when unmasked",
                   to_mm ? "to RAM" : "to ARAM", len);
    }
}

/* whether the DSP interface still asserts interrupt `id` (os_irq.c, just before the handler runs):
 * the hardware's line follows the CSR, so a latched raise whose status the game already acknowledged
 * (and whose mail it already took) is not delivered; the SDK's DSP handler waits for mail on entry
 * with interrupts off, and spun forever on such a stale one */
int dsp_irq_asserted(int id) {
    mtx_lock(&g_lock);
    uint32_t csr = g_csr;
    mtx_unlock(&g_lock);
    switch (id) {
    case OS_INTERRUPT_DSP_DSP: return (csr & CSR_DSPINT) && (csr & CSR_DSPMSK);
    case OS_INTERRUPT_DSP_ARAM: return (csr & CSR_ARINT) && (csr & CSR_ARMSK);
    case OS_INTERRUPT_DSP_AI: return (csr & CSR_AIDINT) && (csr & CSR_AIDMSK);
    default: return 1;
    }
}

void dsp_dump(void) {
    rt_log("[dsp] CSR %04X, ARAM DMA mm %04X%04X ar %04X%04X cnt %04X%04X", g_csr, g_regs[0x20 >> 1], g_regs[0x22 >> 1],
           g_regs[0x24 >> 1], g_regs[0x26 >> 1], g_regs[0x28 >> 1], g_regs[0x2A >> 1]);
}

uint8_t* aram_ptr(uint32_t addr) { return g_aram + (addr % ARAM_SIZE); }

/* ---- registers ---- */
static uint32_t read16(uint32_t off) {
    switch (off) {
    case 0x00: return 0;  /* CPU->DSP mailbox: the DSP has always taken the mail already */
    case 0x02: return 0;
    case 0x04: return g_out_head != g_out_tail ? (0x8000 | (g_out[g_out_head % OUTQ].mail >> 16)) : 0;
    case 0x06: {
        uint32_t v = g_out_head != g_out_tail ? (g_out[g_out_head % OUTQ].mail & 0xFFFF) : 0;
        pop_mail();
        return v;
    }
    case 0x0A: return g_csr & ~CSR_RES;
    case 0x16: return 1;  /* AR_MODE: ARAM ready */
    case 0x3A: return 0;  /* AI DMA bytes left */
    default: return g_regs[off >> 1];
    }
}

static void write16(uint32_t off, uint32_t v) {
    switch (off) {
    case 0x00: g_to_dsp_hi = v; return;
    case 0x02: dsp_mail_from_cpu(g_to_dsp_hi << 16 | (v & 0xFFFF)); return;
    case 0x0A: {
        uint16_t old = g_csr;
        g_csr = (uint16_t)((g_csr & CSR_INTS & ~v) | (v & ~CSR_INTS & ~CSR_RES));
        if (v & CSR_RES) {  /* reset: the DSP ROM starts over */
            g_state = DSP_OFF;
            g_dsp_gen++;
            g_out_head = g_out_tail = 0;
        }
        if ((old & CSR_HALT) && !(v & CSR_HALT) && g_state == DSP_OFF) {  /* unhalted: the ROM boots */
            g_state = DSP_ROM;
            g_boot_step = 0;
            push_mail(0x8071FEEDu, 0);
        }
        /* interrupt status bits that are still set and enabled re-raise, as the hardware's interrupt
         * line is status & mask: an ARAM DMA that completed while its interrupt was masked must
         * still interrupt when it is unmasked (lost, the game's sound loading waited forever) */
        if ((g_csr & CSR_DSPINT) && (g_csr & CSR_DSPMSK)) os_raise_interrupt(OS_INTERRUPT_DSP_DSP);
        if ((g_csr & CSR_ARINT) && (g_csr & CSR_ARMSK)) os_raise_interrupt(OS_INTERRUPT_DSP_ARAM);
        if ((g_csr & CSR_AIDINT) && (g_csr & CSR_AIDMSK)) os_raise_interrupt(OS_INTERRUPT_DSP_AI);
        return;
    }
    case 0x2A:
        g_regs[off >> 1] = (uint16_t)v;
        aram_dma();
        return;
    case 0x36:
        g_regs[off >> 1] = (uint16_t)v;
        g_ai_start = ((uint32_t)g_regs[0x30 >> 1] << 16 | g_regs[0x32 >> 1]) & 0x03FFFFE0;
        g_ai_blocks = v & 0x7FFF;
        atomic_store(&g_ai_dma_on, (v & 0x8000) && g_ai_blocks);
        return;
    default:
        g_regs[off >> 1] = (uint16_t)v;
        return;
    }
}

uint32_t dsp_read(uint32_t ea, int size) {
    uint32_t off = ea & 0x7F;
    mtx_lock(&g_lock);
    uint32_t v = size == 4 ? (read16(off) << 16 | read16(off + 2)) : read16(off & ~1u);
    mtx_unlock(&g_lock);
    return v;
}

void dsp_write(uint32_t ea, uint32_t v, int size) {
    uint32_t off = ea & 0x7F;
    mtx_lock(&g_lock);
    if (size == 4) {
        write16(off, v >> 16);
        write16(off + 2, v & 0xFFFF);
    } else {
        write16(off & ~1u, v & 0xFFFF);
    }
    mtx_unlock(&g_lock);
}

/* ---- Audio Interface registers (0xCC006C00): control, volume, sample counter, interrupt timing ---- */
static uint32_t g_ai[4];
uint32_t ai_read(uint32_t ea, int size) {
    (void)size;
    uint32_t r = (ea & 0x1F) >> 2;
    if (r == 2) {  /* AISCNT: 32 kHz samples; a read that would repeat the last value returns the next one */
        /* __AI_SRC_INIT (boot) times two consecutive counter steps with OSGetTime and retries until
         * the gap is under ~28.5 us or within 34.5-39 us: an exact 32 kHz counter gives 31.25 us,
         * the one gap it rejects, and only host jitter let it through (38 s on the Switch). A polling
         * loop now sees the next step at once; the counter still follows real time. */
        static uint32_t last, bump;
        uint32_t v = (uint32_t)(rt_ticks() * 32000 / 40500000) + bump;
        if (v == last) { bump++; v++; }
        last = v;
        return v;
    }
    return r < 4 ? g_ai[r] : 0;
}
void ai_write(uint32_t ea, uint32_t v, int size) {
    (void)size;
    uint32_t r = (ea & 0x1F) >> 2;
    if (r == 0) v &= ~0x28u;  /* AICR: interrupt status and sample counter reset bits do not stick */
    if (r < 4) g_ai[r] = v;
}

void dsp_init(void) {
    mtx_init(&g_lock, mtx_recursive);
    g_aram = calloc(1, ARAM_SIZE);
    if (!g_aram) rt_fatal("cannot allocate ARAM");
    g_csr = CSR_HALT;
    rt_thread_start(ai_thread, NULL);
    mtx_init(&g_ax_lock, mtx_plain);
    cnd_init(&g_ax_cv);
    const char* sync = getenv("SR_DSP_SYNC");
    g_ax_sync = sync && sync[0] == '1';
    { int ppc_detclock(void); if (ppc_detclock()) g_ax_sync = 1; }  /* SR_DETCLOCK: mixes finish at a fixed point */
    if (g_ax_sync) rt_log("[dsp] SR_DSP_SYNC: audio mixed on the game thread");
    else rt_thread_start(ax_thread, NULL);
}
