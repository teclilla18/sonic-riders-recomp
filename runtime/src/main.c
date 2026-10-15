/* Entry point (host build): sonicriders <path to the GXEE8P disc image> */
#include <stdio.h>

#include "recomp_table.h"
#include "runtime.h"

static Cpu g_main_cpu;

int main(int argc, char** argv) {
    const char* disc = argc > 1 ? argv[1] : "game/SonicRiders.iso";
    mem_init();
    if (!disc_open(disc)) rt_fatal("cannot open the disc image %s", disc);
    load_game();
    cpu_init(&g_main_cpu);
    rt_log("[boot] %u recompiled functions, %u native replacements; starting at %08X",
           g_recomp_func_count, g_recomp_hle_count, g_recomp_entry_point);
    g_main_cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&g_main_cpu);
    rt_log("[boot] the game's entry point returned");
    return 0;
}
