/* Put the game in guest memory the way the IPL and apploader would, then link _Main.rel at the
 * fixed address the recompiler translated it for (tools/recomp/gcrecomp.py REL_BASE). */
#include <stdlib.h>
#include <string.h>

#include "recomp_table.h"
#include "runtime.h"

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }

static void load_dol(void) {
    const uint8_t* h = disc_header();
    uint32_t dol_off = be32(h + 0x420);
    uint8_t dh[0x100];
    disc_read(dol_off, dh, sizeof dh);
    for (int i = 0; i < 18; i++) {
        uint32_t off = be32(dh + 4 * i), addr = be32(dh + 0x48 + 4 * i), size = be32(dh + 0x90 + 4 * i);
        if (!size) continue;
        disc_read((uint64_t)dol_off + off, mem_phys(addr), size);
    }
    uint32_t bss = be32(dh + 0xD8), bss_size = be32(dh + 0xDC);
    /* No clearing here: the header's bss range spans .sdata/.sdata2 too (they sit between .sbss
     * pieces), so clearing it would wipe initialized small data. MEM1 was zeroed before loading,
     * and __init_data clears the real bss sections from _bss_init_info. */
    rt_log("[load] main.dol: entry %08X, bss %08X+%X", be32(dh + 0xE0), bss, bss_size);
}

/* OSLink for a module at a fixed base; module 0 (the DOL) targets are absolute addends */
static void link_rel(const uint8_t* rel, uint32_t rel_size) {
    uint32_t base = g_rel_base;
    uint32_t nsec = be32(rel + 0x0C), secinfo = be32(rel + 0x10), id = be32(rel + 0x00);
    uint32_t imp_off = be32(rel + 0x28), imp_size = be32(rel + 0x2C);
    uint32_t fix_size = be32(rel + 0x48);
    uint32_t sec_addr[32] = {0};
    if (nsec > 32 || fix_size > rel_size) rt_fatal("unexpected _Main.rel layout");
    memcpy(mem_phys(base), rel, fix_size);
    for (uint32_t i = 0; i < nsec; i++) {
        uint32_t o = be32(rel + secinfo + 8 * i) & ~3u, s = be32(rel + secinfo + 8 * i + 4);
        sec_addr[i] = o ? base + o : (s ? g_rel_bss : 0);
    }
    memset(mem_phys(g_rel_bss), 0, g_rel_bss_size);
    uint32_t applied = 0;
    for (uint32_t k = 0; k < imp_size / 8; k++) {
        uint32_t mod = be32(rel + imp_off + 8 * k), p = be32(rel + imp_off + 8 * k + 4);
        uint32_t cur = 0, sec = 0;
        for (;; p += 8) {
            uint16_t o = be16(rel + p);
            uint8_t t = rel[p + 2], ts = rel[p + 3];
            uint32_t add = be32(rel + p + 4);
            if (t == 203) break;                         /* R_DOLPHIN_END */
            if (t == 202) { sec = ts; cur = 0; continue; } /* R_DOLPHIN_SECTION */
            cur += o;
            if (t == 201) continue;                      /* R_DOLPHIN_NOP */
            uint32_t where = sec_addr[sec] + cur;
            uint32_t target = mod == id ? sec_addr[ts] + add : add;
            uint8_t* q = mem_phys(where);
            switch (t) {
            case 1: put32(q, target); break;                                          /* ADDR32 */
            case 2: put32(q, (be32(q) & 0xFC000003u) | (target & 0x03FFFFFCu)); break; /* ADDR24 */
            case 3: case 4: put16(q, (uint16_t)target); break;                        /* ADDR16 / _LO */
            case 5: put16(q, (uint16_t)(target >> 16)); break;                        /* _HI */
            case 6: put16(q, (uint16_t)((target + 0x8000) >> 16)); break;            /* _HA */
            case 7: put32(q, (be32(q) & 0xFFFF0003u) | (target & 0xFFFCu)); break;    /* ADDR14 */
            case 10: put32(q, (be32(q) & 0xFC000003u) | ((target - where) & 0x03FFFFFCu)); break; /* REL24 */
            case 11: put32(q, (be32(q) & 0xFFFF0003u) | ((target - where) & 0xFFFCu)); break;     /* REL14 */
            default: rt_fatal("REL relocation type %u at %08X", t, where);
            }
            applied++;
        }
    }
    rt_log("[load] _Main.rel linked at %08X (bss %08X+%X), %u relocations", base, g_rel_bss, g_rel_bss_size, applied);
}

void load_game(void) {
    const uint8_t* h = disc_header();
    memset(mem_phys(0), 0, GUEST_RAM_SIZE);
    load_dol();

    /* boot info, as the IPL/apploader leave it (YAGCD "Memory map: 0x80000000") */
    memcpy(mem_phys(0x80000000), h, 0x20);             /* disc ID, maker, disc/rev, ... */
    mem_w32(0x80000020, 0x0D15EA5E);                    /* boot magic: normal boot */
    mem_w32(0x80000024, 1);                             /* version */
    mem_w32(0x80000028, MEM1_SIZE);                     /* physical memory size */
    mem_w32(0x8000002C, 0x00000003);                    /* console type: retail HW2 */
    mem_w32(0x800000F0, MEM1_SIZE);                     /* simulated memory size */
    mem_w32(0x800000F8, 162000000);                     /* bus clock */
    mem_w32(0x800000FC, 486000000);                     /* CPU clock */
    mem_w32(0x800000CC, 0);                             /* VI mode: NTSC */

    /* FST at the top of MEM1, as the apploader puts it; ArenaHi just below it */
    uint32_t fst_size;
    const uint8_t* fst = disc_fst(&fst_size);
    uint32_t fst_addr = (0x80000000u + MEM1_SIZE - fst_size) & ~31u;
    mem_write_bytes(fst_addr, fst, fst_size);
    mem_w32(0x80000038, fst_addr);
    mem_w32(0x8000003C, be32(h + 0x42C));
    uint32_t rel_end = g_rel_bss + g_rel_bss_size;
    if (g_rel_base < 0x80000000u + MEM1_SIZE || rel_end > 0x80000000u + GUEST_RAM_SIZE)
        rt_fatal("REL (%08X-%08X) must lie in the backed area above MEM1", g_rel_base, rel_end);
    mem_w32(0x80000030, 0);                             /* ArenaLo: OSInit uses __ArenaLo */
    mem_w32(0x80000034, fst_addr);                      /* ArenaHi */

    uint32_t off, size;
    if (!disc_find("_Main.rel", &off, &size)) rt_fatal("_Main.rel not found on the disc");
    uint8_t* rel = malloc(size);
    disc_read(off, rel, size);
    link_rel(rel, size);
    free(rel);
}
