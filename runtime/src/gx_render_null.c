/* Renderer placeholder: counts what the GP is asked to do. Replaced by the GL (desktop) and deko3d
 * (Switch) backends. */
#include "gx.h"
#include "runtime.h"

static unsigned long long g_copies;

void gx_draw(int prim, int vat, uint32_t count, const uint8_t* vertices, uint32_t vertex_size) {
    (void)prim; (void)vat; (void)count; (void)vertices; (void)vertex_size;
}

void gx_bp_written(uint32_t reg, uint32_t value) {
    if (reg == 0x52) {  /* PE_COPY_EXECUTE: EFB -> texture or XFB */
        g_copies++;
        if ((value >> 14) & 1) {  /* copy to XFB: one frame done */
            static unsigned frames;
            if (++frames % 60 == 1)
                rt_log("[gx] frame %u: %llu draws, %llu FIFO bytes, %llu bytes read from disc, %llu AX frames", frames,
                       g_gx.draws, g_gx.fifo_bytes, di_bytes_read(), ax_list_count());
        }
    }
}
