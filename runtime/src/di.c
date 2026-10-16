/* DVD Interface (0xCC006000): a drive model so the game's own DVD library (DVDLow*, the state
 * machine, the file system, async reads, CRI's streaming on top) runs recompiled. Data is read
 * from the user's disc image when a command starts; the command completes (TSTART clears, the DI
 * interrupt is raised) a few ms later, on a timer thread. Completing inside the register write (or
 * too soon after it) raced the SDK's timeout alarm, which is armed after the command starts: the
 * alarm then fired as a drive timeout and loading stopped at "Now Loading" (about 1 run in 20).
 * Register and command set: YAGCD "DVD Interface", Dolphin's DVDInterface. */
#include <string.h>
#include <threads.h>
#include <time.h>

#include "runtime.h"

#define OS_INTERRUPT_PI_DI 21

enum { DISR, DICVR, DICMD0, DICMD1, DICMD2, DIMAR, DILEN, DICR, DIIMM, DICFG, DI_NREGS };
static uint32_t g_di[DI_NREGS];
static unsigned long long g_read_bytes, g_read_ticks;
static mtx_t g_di_lock;           /* g_di: the CPU's accesses vs. the completion thread */
static cnd_t g_di_cv;
static int64_t g_di_done_at = -1; /* rt_ticks() when the command in progress completes */

/* DISR bits */
#define DEINT_MASK  0x02u
#define DEINT       0x04u
#define TCINT_MASK  0x08u
#define TCINT       0x10u
#define BRKINT_MASK 0x20u
#define BRKINT      0x40u

static void dma_from_disc(uint64_t offset, uint32_t addr, uint32_t len) {
    uint32_t pa = addr & 0x03FFFFFF;
    if (pa + len > GUEST_RAM_SIZE) rt_fatal("DVD read of %u bytes to %08X goes past MEM1", len, addr);
    uint64_t t = rt_ticks();
    size_t got = disc_read(offset, mem_phys(pa), len);
    g_read_ticks += rt_ticks() - t;
    if (got != len) memset(mem_phys(pa) + got, 0, len - got);
    g_read_bytes += len;
}

static void execute(void) {
    uint32_t cmd = g_di[DICMD0] >> 24;
    uint32_t len = g_di[DILEN];
    switch (cmd) {
    case 0xA8: {  /* read: offset in 4-byte units; length; DMA to DIMAR */
        uint64_t off = (uint64_t)g_di[DICMD1] << 2;
        uint32_t n = g_di[DICMD2] ? g_di[DICMD2] : len;
        if (n > len && len) n = len;
        dma_from_disc(off, g_di[DIMAR], n);
        g_di[DIMAR] += n;
        g_di[DILEN] = len - n;
        break;
    }
    case 0x12: {  /* inquiry: drive revision info, 0x20 bytes */
        static const uint8_t info[0x20] = {0x00, 0x00, 0x00, 0x02, 0x20, 0x02, 0x04, 0x02, 0x61};
        uint32_t pa = g_di[DIMAR] & 0x03FFFFFF;
        memcpy(mem_phys(pa), info, len < 0x20 ? len : 0x20);
        g_di[DIMAR] += len;
        g_di[DILEN] = 0;
        break;
    }
    case 0xE0: g_di[DIIMM] = 0; break;     /* request error: none */
    case 0xE2: g_di[DIIMM] = 0; break;     /* audio stream status: not playing */
    case 0xAB:                              /* seek */
    case 0xE1:                              /* audio stream play (unused by this game) */
    case 0xE3:                              /* stop motor */
    case 0xE4:                              /* audio buffer config */
        break;
    default:
        rt_log("[di] unhandled drive command %08X %08X %08X", g_di[DICMD0], g_di[DICMD1], g_di[DICMD2]);
        break;
    }
    /* completes after 2 ms + the transfer at 32 MB/s (a real drive: tens of ms + ~3 MB/s). The SDK
     * arms its timeout alarm after starting a command (DVDLowRead: DICR, then OSSetAlarm); a
     * completion before that leaves the alarm to fire later as a drive timeout, and loading stops. */
    uint64_t bytes = cmd == 0xA8 ? len : 0;
    g_di_done_at = (int64_t)rt_ticks() + 81000 + (int64_t)(bytes * 40500000ull / (32ull << 20));
    cnd_signal(&g_di_cv);
}

static int di_timer(void* arg) {
    (void)arg;
    /* core 2, with the other device threads: on core 0 the game thread, often spinning in guest
     * code, kept this thread from running (Horizon does not time-slice equal priorities), so disc
     * reads completed seconds late (10-30 s stalls at boot, freezes in races) */
    rt_thread_pin(2);
    mtx_lock(&g_di_lock);
    for (;;) {
        if (g_di_done_at < 0) { cnd_wait(&g_di_cv, &g_di_lock); continue; }
        int64_t left = g_di_done_at - (int64_t)rt_ticks();
        if (left <= 0) {
            g_di_done_at = -1;
            g_di[DICR] &= ~1u;
            g_di[DISR] |= TCINT;
            int raise = (g_di[DISR] & TCINT_MASK) != 0;
            mtx_unlock(&g_di_lock);
            if (raise) os_raise_interrupt(OS_INTERRUPT_PI_DI);
            mtx_lock(&g_di_lock);
            continue;
        }
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        int64_t ns = left * 2000 / 81;  /* ticks at 40.5 MHz -> ns */
        ts.tv_sec += ns / 1000000000;
        ts.tv_nsec += ns % 1000000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        cnd_timedwait(&g_di_cv, &g_di_lock, &ts);
    }
    return 0;
}

void di_init(void) {
    mtx_init(&g_di_lock, mtx_plain);
    cnd_init(&g_di_cv);
    rt_thread_start(di_timer, NULL);
}

uint32_t di_read(uint32_t ea, int size) {
    uint32_t r = (ea & 0x3F) >> 2;
    mtx_lock(&g_di_lock);
    uint32_t v = r < DI_NREGS ? g_di[r] : 0;
    mtx_unlock(&g_di_lock);
    if (r == DICVR) v &= ~1u;  /* cover closed */
    if (size == 2) v = (ea & 2) ? (v & 0xFFFF) : (v >> 16);
    return v;
}

void di_write(uint32_t ea, uint32_t v, int size) {
    uint32_t r = (ea & 0x3F) >> 2;
    if (r >= DI_NREGS || size != 4) {
        rt_log("[di] unexpected %d-byte write %08X = %08X", size, ea, v);
        return;
    }
    mtx_lock(&g_di_lock);
    switch (r) {
    case DISR:  /* interrupt status: write 1 to clear; masks and break are stored */
        g_di[DISR] = (g_di[DISR] & (DEINT | TCINT | BRKINT) & ~v) | (v & (DEINT_MASK | TCINT_MASK | BRKINT_MASK | 1u));
        if ((g_di[DISR] & TCINT) && (g_di[DISR] & TCINT_MASK)) os_raise_interrupt(OS_INTERRUPT_PI_DI);  /* status & mask */
        break;
    case DICVR:
        g_di[DICVR] = (g_di[DICVR] & 4u & ~v) | (v & 2u);
        break;
    case DICR:
        g_di[DICR] = v;
        if (v & 1) execute();
        break;
    default:
        g_di[r] = v;
        break;
    }
    mtx_unlock(&g_di_lock);
}

void di_dump(void) {
    rt_log("[di] SR %08X CVR %08X CMD %08X %08X %08X MAR %08X LEN %08X CR %08X; completes in %lld ticks", g_di[DISR],
           g_di[DICVR], g_di[DICMD0], g_di[DICMD1], g_di[DICMD2], g_di[DIMAR], g_di[DILEN], g_di[DICR],
           g_di_done_at < 0 ? -1LL : (long long)(g_di_done_at - (int64_t)rt_ticks()));
}

unsigned long long di_bytes_read(void) { return g_read_bytes; }
unsigned long long di_read_ticks(void) { return g_read_ticks; }
