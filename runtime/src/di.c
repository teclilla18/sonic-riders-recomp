/* DVD Interface (0xCC006000): a drive model so the game's own DVD library (DVDLow*, the state
 * machine, the file system, async reads, CRI's streaming on top) runs recompiled. Commands complete
 * immediately, reading straight from the user's disc image, and raise the DI interrupt.
 * Register and command set: YAGCD "DVD Interface", Dolphin's DVDInterface. */
#include <string.h>

#include "runtime.h"

#define OS_INTERRUPT_PI_DI 21

enum { DISR, DICVR, DICMD0, DICMD1, DICMD2, DIMAR, DILEN, DICR, DIIMM, DICFG, DI_NREGS };
static uint32_t g_di[DI_NREGS];
static unsigned long long g_read_bytes;

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
    size_t got = disc_read(offset, mem_phys(pa), len);
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
    g_di[DICR] &= ~1u;
    g_di[DISR] |= TCINT;
    if (g_di[DISR] & TCINT_MASK) os_raise_interrupt(OS_INTERRUPT_PI_DI);
}

uint32_t di_read(uint32_t ea, int size) {
    uint32_t r = (ea & 0x3F) >> 2;
    uint32_t v = r < DI_NREGS ? g_di[r] : 0;
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
    switch (r) {
    case DISR:  /* interrupt status: write 1 to clear; masks and break are stored */
        g_di[DISR] = (g_di[DISR] & (DEINT | TCINT | BRKINT) & ~v) | (v & (DEINT_MASK | TCINT_MASK | BRKINT_MASK | 1u));
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
}

unsigned long long di_bytes_read(void) { return g_read_bytes; }
