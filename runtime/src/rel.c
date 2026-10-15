/* OSLink / OSLinkFixed. The bootstrap (main) reads _Main.rel into a buffer from its heap and links
 * it there; the recompiled module code instead lives at the fixed base it was translated for, linked
 * at boot by loader.c. So linking only finishes the game's header the way Link would (prolog,
 * epilog, unresolved become absolute addresses: here, those of the fixed image) and reports success.
 * The game then calls module->prolog, which reaches the recompiled code. */
#include "recomp_table.h"
#include "runtime.h"

void hle_OSLink(Cpu* c) {
    uint32_t m = c->r[3], bss = c->r[4];
    uint32_t id = ld32(m + 0x00), version = ld32(m + 0x1C);
    uint32_t want_id = ld32(g_rel_base + 0x00);
    if (id != want_id || version != ld32(g_rel_base + 0x1C))
        rt_fatal("OSLink of an unexpected module (id %u version %u at %08X); only _Main.rel is precompiled", id,
                 version, m);
    st32(m + 0x34, g_rel_prolog);  /* prolog */
    st32(m + 0x38, 0);             /* epilog: none in this module */
    st32(m + 0x3C, 0);             /* unresolved: none */
    rt_log("[rel] OSLink(%08X, bss %08X): module %u runs from its fixed image at %08X (prolog %08X)", m, bss, id,
           g_rel_base, g_rel_prolog);
    c->r[3] = 1;
}
