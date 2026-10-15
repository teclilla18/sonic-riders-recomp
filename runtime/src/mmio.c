/* Hardware register / FIFO / EFB accesses from recompiled code (ppc.h routes 0xC8000000-0xCFFFFFFF
 * here). For now every access is logged; devices (CP/PE FIFO, GX pipe, VI, DSP mailbox, AI) will be
 * modelled here only where recompiled libraries touch them (see NOTES.md "HLE boundary"). */
#include "runtime.h"

static unsigned g_logged;

static void note(const char* what, uint32_t ea, uint32_t v, int size) {
    if (g_logged < 200) rt_log("[mmio] %s%d %08X = %08X", what, size * 8, ea, v);
    else if (g_logged == 200) rt_log("[mmio] (further accesses not logged)");
    g_logged++;
}

uint32_t mmio_read(uint32_t ea, int size) {
    note("read", ea, 0, size);
    return 0;
}

void mmio_write(uint32_t ea, uint32_t v, int size) { note("write", ea, v, size); }

void mmio_write64(uint32_t ea, uint64_t v) {
    mmio_write(ea, (uint32_t)(v >> 32), 4);
    mmio_write(ea + 4, (uint32_t)v, 4);
}
