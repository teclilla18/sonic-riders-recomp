/* Small native replacements shared by several SDK functions (config/GXEE8P/hle.txt). */
#include "runtime.h"

/* hardware set-up/tear-down that has nothing to do natively; returns 0 */
void hle_nop(Cpu* c) { c->r[3] = 0; }

/* The SDK's memcpy (overlap-safe, byte at a time), memset and memmove. Natively when both ranges
 * lie inside MEM1 or the locked cache; anything else (hardware registers, odd addresses) runs the
 * game's own code. Only r3 (the destination) is meaningful on return. */
void f_80003540_orig(Cpu* c);  /* the game's own code (generated) */
void f_8003A354_orig(Cpu* c);
void f_80003458_orig(Cpu* c);

static int plain_range(uint32_t ea, uint32_t n) {
    uint32_t pa = ea & PPC_PHYS_MASK;
    if (PPC_IS_MMIO(ea)) return 0;
    if (pa < 0x01800000u) return n <= 0x01800000u - pa;
    if (pa >= 0x20000000u && pa < 0x20004000u) return n <= 0x20004000u - pa;
    return 0;
}

static void hle_move(Cpu* c, void (*orig)(Cpu*)) {
    uint32_t d = c->r[3], s = c->r[4], n = c->r[5];
    if (!plain_range(d, n) || !plain_range(s, n)) { orig(c); return; }
    memmove(ppc_ptr(d), ppc_ptr(s), n);
}
void hle_memcpy(Cpu* c) { hle_move(c, f_80003540_orig); }
void hle_memmove(Cpu* c) { hle_move(c, f_8003A354_orig); }

void hle_memset(Cpu* c) {
    uint32_t d = c->r[3], n = c->r[5];
    if (!plain_range(d, n)) { f_80003458_orig(c); return; }
    memset(ppc_ptr(d), (int)(c->r[4] & 0xFF), n);
}
