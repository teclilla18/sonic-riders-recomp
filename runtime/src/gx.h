/* GX (Flipper graphics) state as seen through the command stream (gx_fifo.c), and the renderer
 * interface it feeds. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RAM addresses in GX registers (texture images, TLUT sources, copy destinations) are physical: the
 * hardware ignores the bits above them. Sonic Riders sets bit 27 in some TLUT loads (088B6AC0 for
 * 008B6AC0): unmasked, the load was skipped as out of range and the texture got a stale palette
 * (the rainbow trick-rank letters). Dolphin masks the same way. */
#define GX_PHYS_MASK 0x01FFFFFFu

typedef struct GxState {
    uint32_t cp[256];         /* CP registers (VCD, VAT, array bases/strides, matrix indices) */
    uint32_t bp[256];         /* BP registers (24-bit values) */
    uint32_t bp_mask;
    uint32_t konst[8];        /* TEV konstant colors: BP 0xE0-0xE7 written with bit 23 set */
    uint32_t xf_mem[0x1000];  /* XF memory: position/normal/texture/post matrices, lights */
    uint32_t xf_reg[0x100];   /* XF registers 0x1000-0x10FF */
    uint32_t xfm_dirty;       /* XF memory chunks (64 words, GX_XFM_CHUNKS of them) changed since read */
    int regs_dirty;           /* a BP or XF register (or konst) was written since the last state snapshot */
    unsigned long long fifo_bytes, draws;
    unsigned bad_logged;
} GxState;

extern GxState g_gx;

void gx_init(void);
int gx_async(void);
void gx_pipe_idle(void);  /* flush the write-gather buffer: the game thread goes idle */  /* the FIFO is parsed on the GP thread (gx_fifo.c) */
int gx_vertex_size(int vat);

/* gx_fifo.c: device side */
void gx_pipe_write(uint32_t v, int size);
uint32_t gx_cp_read(uint32_t off, int size);
void gx_cp_write(uint32_t off, uint32_t v, int size);
uint32_t gx_pe_read(uint32_t off, int size);
void gx_pe_write(uint32_t off, uint32_t v, int size);
uint32_t gx_pi_read(uint32_t off, int size);
void gx_pi_write(uint32_t off, uint32_t v, int size);

/* renderer hooks (gx_render*.c) */
void gx_draw(int prim, int vat, uint32_t count, const uint8_t* vertices, uint32_t vertex_size);
void gx_bp_written(uint32_t reg, uint32_t value);  /* e.g. EFB copies (0x52), texture loads */

#ifdef __cplusplus
}
#endif
