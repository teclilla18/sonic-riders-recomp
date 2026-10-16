/* Hardware register / FIFO / EFB accesses from recompiled code (ppc.h routes 0xC8000000-0xCFFFFFFF
 * here). For now: the GX write-gather pipe is counted, every other access is logged (first ones).
 * Devices get modelled here only where recompiled libraries touch them (NOTES.md "HLE boundary"). */
#include "gx.h"
#include "runtime.h"

#define GX_FIFO 0xCC008000u

static unsigned g_logged;
static unsigned long long g_fifo_bytes;
static uint32_t g_last[16][3];
static unsigned g_last_pos;

static void note(const char* what, uint32_t ea, uint32_t v, int size) {
    g_last[g_last_pos % 16][0] = ea;
    g_last[g_last_pos % 16][1] = v;
    g_last[g_last_pos % 16][2] = (uint32_t)size | (what[0] == 'w' ? 0x100u : 0);
    g_last_pos++;
    if (g_logged < 300) rt_log("[mmio] %s%d %08X = %08X", what, size * 8, ea, v);
    else if (g_logged == 300) rt_log("[mmio] (further accesses not logged)");
    g_logged++;
}

/* modelled devices, by 0xCC00xxxx block */
#define BLOCK(ea) ((ea) & 0xFFFFFF00u)
#define CP_BASE 0xCC000000u
#define PE_BASE 0xCC001000u
#define VI_BASE 0xCC002000u
#define PI_BASE 0xCC003000u
#define DI_BASE 0xCC006000u
#define DSP_BASE 0xCC005000u
#define AI_BASE 0xCC006C00u
#define EXI_BASE 0xCC006800u
#define SI_BASE 0xCC006400u

uint32_t mmio_read(uint32_t ea, int size) {
    if (BLOCK(ea) == VI_BASE) return vi_read(ea, size);
    if (BLOCK(ea) == CP_BASE) return gx_cp_read(ea, size);
    if (BLOCK(ea) == PE_BASE) return gx_pe_read(ea, size);
    if (BLOCK(ea) == PI_BASE) return gx_pi_read(ea, size);
    if ((ea & 0xFFFFFFC0u) == DI_BASE) return di_read(ea, size);
    if (BLOCK(ea) == DSP_BASE) return dsp_read(ea, size);
    if ((ea & 0xFFFFFFE0u) == AI_BASE) return ai_read(ea, size);
    if (ea - EXI_BASE < 0x3Cu) return exi_read(ea, size);
    if (BLOCK(ea) == SI_BASE) return si_read(ea, size);
    note("read", ea, 0, size);
    return 0;
}

void mmio_write(uint32_t ea, uint32_t v, int size) {
    if ((ea & ~31u) == GX_FIFO) { g_fifo_bytes += (unsigned)size; gx_pipe_write(v, size); return; }
    if (BLOCK(ea) == VI_BASE) { vi_write(ea, v, size); return; }
    if (BLOCK(ea) == CP_BASE) { gx_cp_write(ea, v, size); return; }
    if (BLOCK(ea) == PE_BASE) { gx_pe_write(ea, v, size); return; }
    if (BLOCK(ea) == PI_BASE) { gx_pi_write(ea, v, size); return; }
    if ((ea & 0xFFFFFFC0u) == DI_BASE) { di_write(ea, v, size); return; }
    if (BLOCK(ea) == DSP_BASE) { dsp_write(ea, v, size); return; }
    if ((ea & 0xFFFFFFE0u) == AI_BASE) { ai_write(ea, v, size); return; }
    if (ea - EXI_BASE < 0x3Cu) { exi_write(ea, v, size); return; }
    if (BLOCK(ea) == SI_BASE) { si_write(ea, v, size); return; }
    note("write", ea, v, size);
}

void mmio_write64(uint32_t ea, uint64_t v) {
    mmio_write(ea, (uint32_t)(v >> 32), 4);
    mmio_write(ea + 4, (uint32_t)v, 4);
}

void mmio_dump(void) {
    rt_log("[mmio] GX FIFO bytes written: %llu; last register accesses:", g_fifo_bytes);
    for (unsigned i = 0; i < 16; i++) {
        unsigned k = (g_last_pos + i) % 16;
        if (g_last[k][0])
            rt_log("[mmio]   %s%u %08X = %08X", (g_last[k][2] & 0x100) ? "write" : "read", (g_last[k][2] & 0xFF) * 8,
                   g_last[k][0], g_last[k][1]);
    }
}
