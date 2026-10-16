/* The GameCube graphics pipe, from the CPU side:
 *
 *  - PI FIFO (0xCC00300C base, 0x3010 end, 0x3014 write pointer): every byte the game writes to the
 *    write-gather pipe (0xCC008000) is stored in RAM at the write pointer, which advances and wraps.
 *    That is how GXBeginDisplayList/GXEndDisplayList record display lists.
 *  - When the GP is linked to the CPU FIFO (CP control: read enable + GP link, immediate mode), the
 *    same bytes go to the command parser below, which decodes CP/XF/BP register loads, indexed
 *    loads, display-list calls and draws. Draws go to the renderer (gx_draw hook).
 *  - PE: the "draw done" (BP 0x45) and token (BP 0x47/0x48) commands raise the PE finish/token
 *    interrupts, so GXDrawDone, GXSetDrawSync and draw-sync callbacks run through the game's own
 *    handlers. The GP is treated as infinitely fast: commands complete as they are parsed.
 *
 * Command formats follow YAGCD / Dolphin's OpcodeDecoding and VertexLoader (vertex sizes). */
#include <stdlib.h>
#include <string.h>

#include "gx.h"
#include "runtime.h"

#define OS_INTERRUPT_PI_PE_TOKEN  18
#define OS_INTERRUPT_PI_PE_FINISH 19

GxState g_gx;

/* ---- PI FIFO ---- */
static uint32_t g_pi_base, g_pi_end, g_pi_wptr;  /* physical; wptr bit 26 = wrapped */
static uint32_t g_pi_regs[0x40];

/* ---- CP / PE registers (16-bit) ---- */
static uint16_t g_cp_mmio[0x40];
static uint16_t g_pe_mmio[0x80];
#define CP_CR      (g_cp_mmio[0x02 >> 1])
#define PE_INTRSR  (g_pe_mmio[0x0A >> 1])
#define PE_TOKEN   (g_pe_mmio[0x0E >> 1])

/* ---- command parser ---- */
static uint8_t* g_pend;
static size_t g_pend_len, g_pend_cap;

static void pend_push(const uint8_t* p, size_t n) {
    if (g_pend_len + n > g_pend_cap) {
        g_pend_cap = (g_pend_len + n) * 2 + 4096;
        g_pend = realloc(g_pend, g_pend_cap);
        if (!g_pend) rt_fatal("out of memory (GX FIFO)");
    }
    memcpy(g_pend + g_pend_len, p, n);
    g_pend_len += n;
}

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* size of one vertex for VAT `vat`, from the current VCD/VAT CP registers */
static const int k_comp_size[8] = {1, 1, 2, 2, 4, 0, 0, 0};
static const int k_color_size[8] = {2, 3, 4, 2, 3, 4, 0, 0};

int gx_vertex_size(int vat) {
    uint32_t lo = g_gx.cp[0x50], hi = g_gx.cp[0x60];
    uint32_t a = g_gx.cp[0x70 + vat], b = g_gx.cp[0x80 + vat], cc = g_gx.cp[0x90 + vat];
    int size = 0;
    for (int i = 0; i < 9; i++) size += (lo >> i) & 1;  /* PNMTXIDX, TEX0..7 MTXIDX: one byte each */
    static const int idx_size[4] = {0, 0, 1, 2};
    /* position */
    int t = (lo >> 9) & 3;
    if (t == 1) size += ((a & 1) ? 3 : 2) * k_comp_size[(a >> 1) & 7];
    else size += idx_size[t];
    /* normal */
    t = (lo >> 11) & 3;
    if (t == 1) size += ((a >> 9) & 1 ? 9 : 3) * k_comp_size[(a >> 10) & 7];
    else if (t >= 2) size += idx_size[t] * (((a >> 9) & 1) && ((a >> 31) & 1) ? 3 : 1);
    /* colors */
    for (int c = 0; c < 2; c++) {
        t = (lo >> (13 + 2 * c)) & 3;
        int fmt = c == 0 ? (a >> 14) & 7 : (a >> 18) & 7;
        if (t == 1) size += k_color_size[fmt];
        else size += idx_size[t];
    }
    /* texture coordinates */
    static const struct { int word, cnt, fmt; } tex[8] = {
        {0, 21, 22}, {1, 0, 1}, {1, 9, 10}, {1, 18, 19}, {1, 27, 28}, {2, 5, 6}, {2, 14, 15}, {2, 23, 24}};
    for (int i = 0; i < 8; i++) {
        t = (hi >> (2 * i)) & 3;
        uint32_t w = tex[i].word == 0 ? a : tex[i].word == 1 ? b : cc;
        if (t == 1) size += (((w >> tex[i].cnt) & 1) ? 2 : 1) * k_comp_size[(w >> tex[i].fmt) & 7];
        else size += idx_size[t];
    }
    return size;
}

static void pe_finish(void) {
    PE_INTRSR |= 0x8;  /* finish status */
    if (PE_INTRSR & 0x2) os_raise_interrupt(OS_INTERRUPT_PI_PE_FINISH);
}

static void pe_token(uint16_t token, int irq) {
    PE_TOKEN = token;
    if (irq) {
        PE_INTRSR |= 0x4;  /* token status */
        if (PE_INTRSR & 0x1) os_raise_interrupt(OS_INTERRUPT_PI_PE_TOKEN);
    }
}

static void load_bp(uint32_t v) {
    uint32_t reg = v >> 24, val = v & 0xFFFFFF;
    if (reg >= 0xE0 && reg <= 0xE7 && (val >> 23)) {  /* konstant color: same address, type bit set */
        g_gx.konst[reg - 0xE0] = val;
        gx_bp_written(reg, val);
        return;
    }
    if (reg != 0xFE) {  /* BP mask register: the next write only changes the masked bits */
        uint32_t m = g_gx.bp_mask;
        g_gx.bp[reg] = (g_gx.bp[reg] & ~m) | (val & m);
        g_gx.bp_mask = 0xFFFFFF;
    } else {
        g_gx.bp_mask = val;
        return;
    }
    switch (reg) {
    case 0x45: pe_finish(); break;                          /* PE_DONE: draw done */
    case 0x47: pe_token((uint16_t)val, 0); break;           /* PE_TOKEN */
    case 0x48: pe_token((uint16_t)val, 1); break;           /* PE_TOKEN_INT */
    default: break;
    }
    gx_bp_written(reg, g_gx.bp[reg]);
}

void gx_state_dirty(void);

static void load_xf(uint32_t addr, const uint8_t* data, uint32_t n) {
    gx_state_dirty();
    for (uint32_t i = 0; i < n; i++, addr++) {
        uint32_t v = rd32(data + 4 * i);
        if (addr < 0x1000) { g_gx.xf_mem[addr] = v; g_gx.xfm_dirty |= 1u << ((addr >> 6) & 31); }
        else if (addr < 0x1100) g_gx.xf_reg[addr - 0x1000] = v;
    }
}

/* LOAD_INDX_A..D: copy n words from an indexed array element into XF memory */
static void load_indexed(int array, uint32_t v) {
    uint32_t index = v >> 16, n = ((v >> 12) & 0xF) + 1, addr = v & 0xFFF;
    uint32_t base = g_gx.cp[0xA0 + 12 + array] & 0x3FFFFFFF, stride = g_gx.cp[0xB0 + 12 + array] & 0xFF;
    uint32_t src = 0x80000000u | (base + index * stride);
    gx_state_dirty();
    for (uint32_t i = 0; i < n && addr + i < 0x1000; i++) {
        g_gx.xf_mem[addr + i] = ld32(src + 4 * i);
        g_gx.xfm_dirty |= 1u << (((addr + i) >> 6) & 31);
    }
}

static size_t parse(const uint8_t* p, size_t len, int depth);

static void call_display_list(uint32_t addr, uint32_t size, int depth) {
    if (depth > 4 || size > 0x4000000) return;
    const uint8_t* p = mem_phys(addr & 0x03FFFFFF);
    parse(p, size, depth + 1);
}

/* Parse complete commands from p[0..len); returns the bytes consumed (a partial command stays). */
/* at depth 0: the bytes the incomplete command at the end of the buffer needs in total (0: unknown),
 * so the write-gather pipe does not re-parse it on every store until it is complete */
static size_t g_parse_need;

static size_t parse(const uint8_t* p, size_t len, int depth) {
    size_t i = 0;
    if (depth == 0) g_parse_need = 0;
    while (i < len) {
        uint8_t cmd = p[i];
        size_t left = len - i;
        if (cmd == 0x00) { i++; continue; }                             /* NOP */
        if (cmd == 0x08) {                                              /* LOAD_CP_REG */
            if (left < 6) break;
            g_gx.cp[p[i + 1]] = rd32(p + i + 2);
            i += 6;
            continue;
        }
        if (cmd == 0x10) {                                              /* LOAD_XF_REG */
            if (left < 5) break;
            uint32_t h = rd32(p + i + 1), n = (h >> 16) + 1;
            if (left < 5 + 4 * n) {
                if (depth == 0) g_parse_need = 5 + 4 * n;
                break;
            }
            load_xf(h & 0xFFFF, p + i + 5, n);
            i += 5 + 4 * n;
            continue;
        }
        if (cmd == 0x20 || cmd == 0x28 || cmd == 0x30 || cmd == 0x38) { /* LOAD_INDX_A..D */
            if (left < 5) break;
            load_indexed((cmd >> 3) & 3, rd32(p + i + 1));
            i += 5;
            continue;
        }
        if (cmd == 0x40) {                                              /* CALL_DL */
            if (left < 9) break;
            call_display_list(rd32(p + i + 1), rd32(p + i + 5), depth);
            i += 9;
            continue;
        }
        if (cmd == 0x44 || cmd == 0x48) { i++; continue; }              /* vertex cache invalidate */
        if (cmd == 0x61) {                                              /* LOAD_BP_REG */
            if (left < 5) break;
            load_bp(rd32(p + i + 1));
            i += 5;
            continue;
        }
        if ((cmd & 0xC0) == 0x80) {                                     /* draw primitive */
            if (left < 3) break;
            int vat = cmd & 7, prim = (cmd >> 3) & 7;
            uint32_t count = rd16(p + i + 1);
            size_t vsize = (size_t)gx_vertex_size(vat), total = 3 + vsize * count;
            if (left < total) {
                if (depth == 0) g_parse_need = total;
                break;
            }
            gx_draw(prim, vat, count, p + i + 3, (uint32_t)vsize);
            g_gx.draws++;
            i += total;
            continue;
        }
        if (!g_gx.bad_logged++)
            rt_log("[gx] unknown FIFO command %02X (depth %d); skipping a byte", cmd, depth);
        i++;
    }
    return i;
}

static size_t g_need;  /* pending bytes needed before parsing again (the incomplete command's size) */
unsigned long long g_pipe_stores, g_pipe_bytes;  /* for the [perf] line */

static void gp_feed(const uint8_t* p, size_t n) {
    pend_push(p, n);
    if (g_pend_len < g_need) return;  /* the command being written is not complete yet */
    size_t used = parse(g_pend, g_pend_len, 0);
    if (used) {
        memmove(g_pend, g_pend + used, g_pend_len - used);
        g_pend_len -= used;
    }
    g_need = g_parse_need;
}

/* ---- write-gather pipe ---- */
void gx_pipe_write(uint32_t v, int size) {
    uint8_t b[4];
    for (int k = 0; k < size; k++) b[k] = (uint8_t)(v >> (8 * (size - 1 - k)));
    g_pipe_stores++;
    g_pipe_bytes += (unsigned)size;
    /* into RAM at the PI write pointer: in one piece unless the FIFO wraps inside this store */
    uint32_t w = g_pi_wptr & 0x03FFFFFF;
    uint32_t end = g_pi_end ? (g_pi_end & 0x03FFFFFF) + 32 : 0;  /* end is inclusive of the last 32 bytes */
    if (g_pi_end && w + (uint32_t)size < end && w + (uint32_t)size <= GUEST_RAM_SIZE) {
        memcpy(mem_phys(w), b, (size_t)size);
        g_pi_wptr = (g_pi_wptr & 0x04000000) | (w + (uint32_t)size);
    } else {
        for (int k = 0; k < size; k++) {
            w = g_pi_wptr & 0x03FFFFFF;
            if (g_pi_end && w < GUEST_RAM_SIZE) *mem_phys(w) = b[k];
            w++;
            if (g_pi_end && w >= end) {
                w = g_pi_base & 0x03FFFFFF;
                g_pi_wptr = w | 0x04000000;
            } else {
                g_pi_wptr = (g_pi_wptr & 0x04000000) | w;
            }
        }
    }
    g_gx.fifo_bytes += (unsigned)size;
    if ((CP_CR & 0x11) == 0x11) gp_feed(b, (size_t)size);  /* GP read enable + linked: immediate mode */
}

/* ---- register blocks ---- */
uint32_t gx_cp_read(uint32_t off, int size) {
    off &= 0x7F;
    if (off == 0x00) return 0x000C;  /* SR: GP read idle + command idle */
    if (size == 4) return (uint32_t)g_cp_mmio[off >> 1] << 16 | g_cp_mmio[(off >> 1) + 1];
    return g_cp_mmio[off >> 1];
}
void gx_cp_write(uint32_t off, uint32_t v, int size) {
    off &= 0x7F;
    if (size == 4) { g_cp_mmio[off >> 1] = (uint16_t)(v >> 16); g_cp_mmio[(off >> 1) + 1] = (uint16_t)v; }
    else g_cp_mmio[off >> 1] = (uint16_t)v;
}

uint32_t gx_pe_read(uint32_t off, int size) {
    off &= 0xFF;
    if (size == 4) return (uint32_t)g_pe_mmio[off >> 1] << 16 | g_pe_mmio[(off >> 1) + 1];
    return g_pe_mmio[off >> 1];
}
void gx_pe_write(uint32_t off, uint32_t v, int size) {
    off &= 0xFF;
    if (off == 0x0A) {  /* writing a 1 to a status bit clears it; the enable bits are stored */
        PE_INTRSR = (uint16_t)((PE_INTRSR & 0xC & ~v) | (v & 0x3));
        return;
    }
    if (size == 4) { g_pe_mmio[off >> 1] = (uint16_t)(v >> 16); g_pe_mmio[(off >> 1) + 1] = (uint16_t)v; }
    else g_pe_mmio[off >> 1] = (uint16_t)v;
}

/* PI: interrupt cause/mask (0x00/0x04), FIFO base/end/write pointer (0x0C/0x10/0x14) */
uint32_t gx_pi_read(uint32_t off, int size) {
    (void)size;
    off &= 0xFF;
    switch (off) {
    case 0x0C: return g_pi_base;
    case 0x10: return g_pi_end;
    case 0x14: return g_pi_wptr;
    case 0x2C: return 0x20000000u | 0x2;  /* console type: retail, Flipper rev */
    default: return g_pi_regs[off >> 2];
    }
}
void gx_pi_write(uint32_t off, uint32_t v, int size) {
    (void)size;
    off &= 0xFF;
    switch (off) {
    case 0x0C: g_pi_base = v & 0x03FFFFE0; break;
    case 0x10: g_pi_end = v & 0x03FFFFE0; break;
    case 0x14: g_pi_wptr = v & 0x07FFFFE0; break;
    default: g_pi_regs[off >> 2] = v; break;
    }
}

void gx_init(void) { g_gx.bp_mask = 0xFFFFFF; }
