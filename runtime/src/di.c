/* DVD Interface (0xCC006000): a drive model so the game's own DVD library (DVDLow*, the state
 * machine, the file system, async reads, CRI's streaming on top) runs recompiled. A read's data
 * reaches RAM gradually during the transfer and the command completes (TSTART clears, the DI
 * interrupt is raised) when the last piece has landed, on a timer thread. Completing inside the register write (or
 * too soon after it) raced the SDK's timeout alarm, which is armed after the command starts: the
 * alarm then fired as a drive timeout and loading stopped at "Now Loading" (about 1 run in 20).
 * Register and command set: YAGCD "DVD Interface", Dolphin's DVDInterface. */
#include <string.h>
#include <threads.h>
#include <time.h>

#include "game_addrs.h"
#include <stdlib.h>

#include "runtime.h"

#define OS_INTERRUPT_PI_DI 21

enum { DISR, DICVR, DICMD0, DICMD1, DICMD2, DIMAR, DILEN, DICR, DIIMM, DICFG, DI_NREGS };
static uint32_t g_di[DI_NREGS];
static unsigned long long g_read_bytes, g_read_ticks;
static mtx_t g_di_lock;           /* g_di: the CPU's accesses vs. the completion thread */
static cnd_t g_di_cv;
static int64_t g_di_done_at = -1; /* rt_ticks() when the command in progress completes */
static _Atomic int g_det_pending;  /* SR_DETCLOCK: a command waits for di_det_poll() */
/* a read's data arrives in pieces during the transfer, as on the hardware (the DI DMAs each 32-byte
 * block as the drive delivers it). The game's streaming decompressor (CRI's loader, REL f_819594F4)
 * starts on a chunk while the rest of it is still arriving; with the whole chunk landing only at
 * completion it decompressed stale memory, and 4-player races crashed loading the "1M" stage pack. */
#define DMA_PIECE 0x4000u
static struct { uint64_t offset; uint32_t addr, len, done; int64_t start; } g_di_dma;
/* the transfer rate (SR_DVD_MBPS, MB/s); ticks per KB at 40.5 MHz */
static int64_t g_ticks_per_kb = 40500000ll * 1024 / (32ll << 20);
#define TICKS_PER_KB g_ticks_per_kb
static uint64_t g_late_sum, g_late_max, g_late_n;  /* completion lateness, for [perf] (under g_di_lock) */
void di_late_stats(uint64_t* avg, uint64_t* max, uint64_t* n) {
    mtx_lock(&g_di_lock);
    *n = g_late_n;
    *avg = g_late_n ? g_late_sum / g_late_n : 0;
    *max = g_late_max;
    g_late_sum = g_late_max = g_late_n = 0;
    mtx_unlock(&g_di_lock);
}

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

/* the read in progress has delivered `upto` bytes: copy the new ones into RAM (DIMAR/DILEN follow) */
static void dma_piece(uint32_t upto) {
    uint32_t n = upto - g_di_dma.done;
    if (!n) return;
    dma_from_disc(g_di_dma.offset + g_di_dma.done, g_di_dma.addr + g_di_dma.done, n);
    g_di_dma.done = upto;
    g_di[DIMAR] += n;
    g_di[DILEN] -= n;
    if (upto == g_di_dma.len) {
        static int log_done = -1;
        if (log_done < 0) log_done = getenv("SR_DVDLOG") != NULL;
        if (log_done) rt_log("[dvdlog]   done: %X bytes at %08X (field %u)", g_di_dma.len, g_di_dma.addr, vi_field_count());
    }
}

static void execute(void) {
    uint32_t cmd = g_di[DICMD0] >> 24;
    uint32_t len = g_di[DILEN];
    switch (cmd) {
    case 0xA8: {  /* read: offset in 4-byte units; length; DMA to DIMAR */
        uint64_t off = (uint64_t)g_di[DICMD1] << 2;
        uint32_t n = g_di[DICMD2] ? g_di[DICMD2] : len;
        if (n > len && len) n = len;
        g_di_dma.offset = off;
        g_di_dma.addr = g_di[DIMAR];
        g_di_dma.len = n;
        g_di_dma.done = 0;
        g_di_dma.start = (int64_t)rt_ticks() + 81000;  /* after a 2 ms seek */
        static int log_reads = -1;  /* SR_DVDLOG=1: every disc read, by file */
        if (log_reads < 0) log_reads = getenv("SR_DVDLOG") != NULL;
        if (log_reads) {
            uint32_t rel;
            const char* name = disc_name_at(off, &rel);
            rt_log("[dvdlog] %s +%X, %X bytes -> %08X (field %u)", name, rel, n, g_di[DIMAR], vi_field_count());
        }
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
    dbg_event(3, cmd == 0xA8 ? g_di[DICMD1] << 2 : g_di[DICMD0], len);
    {   /* SR_DETCLOCK (online play groundwork): the data lands now and the command completes at the next
         * deterministic point (di_det_poll: the next game frame end, or the CPU going idle), not after
         * a time on the host clock, so every run sees the same disc timing */
        int ppc_detclock(void);
        if (ppc_detclock()) {
            void det_trace(char type, uint32_t what);
            det_trace('D', cmd == 0xA8 ? g_di[DICMD1] << 2 : cmd);
            if (g_di_dma.len) { dma_piece(g_di_dma.len); g_di_dma.len = 0; }
            g_det_pending = 1;
            return;
        }
    }
    g_di_done_at = (int64_t)rt_ticks() + 81000 + (int64_t)(bytes * TICKS_PER_KB / 1024) + 1;
    cnd_signal(&g_di_cv);
}

int di_det_busy(void) { return g_det_pending != 0; }
/* SR_DETCLOCK: complete the command started by execute() (game thread: frame end or idle) */
void di_det_poll(void) {
    if (!g_det_pending) return;
    mtx_lock(&g_di_lock);
    g_det_pending = 0;
    dbg_event(4, 0, 0);
    g_di[DICR] &= ~1u;
    g_di[DISR] |= TCINT;
    int raise = (g_di[DISR] & TCINT_MASK) != 0;
    mtx_unlock(&g_di_lock);
    if (raise) os_raise_interrupt(OS_INTERRUPT_PI_DI);
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
            g_late_sum += (uint64_t)-left;  /* how late this thread got to the completion */
            if ((uint64_t)-left > g_late_max) g_late_max = (uint64_t)-left;
            g_late_n++;
            g_di_done_at = -1;
            if (g_di_dma.len) {
                dma_piece(g_di_dma.len);
                g_di_dma.len = 0;
            }
            dbg_event(4, 0, 0);
            g_di[DICR] &= ~1u;
            g_di[DISR] |= TCINT;
            int raise = (g_di[DISR] & TCINT_MASK) != 0;
            mtx_unlock(&g_di_lock);
            if (raise) os_raise_interrupt(OS_INTERRUPT_PI_DI);
            mtx_lock(&g_di_lock);
            continue;
        }
        /* the pieces that have arrived by now; then sleep until the next one (or the completion) */
        static int at_end = -1;  /* SR_DVD_ATEND=1 (test): the old model, data only at completion */
        if (at_end < 0) at_end = getenv("SR_DVD_ATEND") != NULL;
        if (g_di_dma.len && !at_end) {
            int64_t t = (int64_t)rt_ticks() - g_di_dma.start;
            uint32_t arrived = t <= 0 ? 0 : (uint32_t)((uint64_t)t * 1024 / TICKS_PER_KB) & ~(DMA_PIECE - 1);
            if (arrived > g_di_dma.done) dma_piece(arrived < g_di_dma.len ? arrived : g_di_dma.len);
            if (g_di_dma.done < g_di_dma.len) {
                int64_t next = g_di_dma.start + (int64_t)(g_di_dma.done + DMA_PIECE) * TICKS_PER_KB / 1024 - (int64_t)rt_ticks();
                if (next < left) left = next > 0 ? next : 1;
            }
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
    const char* r = getenv("SR_DVD_MBPS");
    if (r && atoi(r) > 0) {
        g_ticks_per_kb = 40500000ll * 1024 / ((int64_t)atoi(r) << 20);
        rt_log("[di] transfer rate %d MB/s", atoi(r));
    }
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
    /* what DVDGetDriveStatus reports: fatal error, pausing, or the executing block's state */
    uint32_t ex = ld32(DVD_EXECUTING);
    rt_log("[dvd] fatal %u pause %u pausing %u curr command %u resume-from-here %u; executing %08X%s", ld32(DVD_FATAL_ERROR_FLAG),
           ld32(DVD_PAUSE_FLAG), ld32(DVD_PAUSING_FLAG), ld32(DVD_CURR_COMMAND), ld32(DVD_RESUME_FROM_HERE), ex,
           ex == DVD_DUMMY_BLOCK ? " (dummy)" : "");
    if (ex >= 0x80000000u && ex < 0x81800000u)
        rt_log("[dvd]   block: command %u state %d offset %08X length %08X addr %08X transferred %08X callback %08X",
               ld32(ex + 8), (int32_t)ld32(ex + 0xC), ld32(ex + 0x10), ld32(ex + 0x14), ld32(ex + 0x18), ld32(ex + 0x20),
               ld32(ex + 0x2C));
}

unsigned long long di_bytes_read(void) { return g_read_bytes; }
unsigned long long di_read_ticks(void) { return g_read_ticks; }
