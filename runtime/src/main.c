/* Entry point (host build): sonicriders <path to the GXEE8P disc image> */
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "gx.h"
#include "gx_render.h"
#include "recomp_table.h"
#include "runtime.h"

static Cpu g_main_cpu;

extern int g_ppc_trace;
void cpu_dump_trace(void);
void mmio_dump(void);
void sched_dump(void);

/* SR_WATCHDOG=N: record function entries and, after N seconds, dump where the game is and exit */
static int watchdog(void* arg) {
    struct timespec ts = {(time_t)(intptr_t)arg, 0};
    thrd_sleep(&ts, NULL);
    rt_log("[watchdog] %d s elapsed", (int)(intptr_t)arg);
    cpu_dump_trace();
    mmio_dump();
    sched_dump();
    fflush(stderr);
    _Exit(3);
}

int main(int argc, char** argv) {
    const char* disc = argc > 1 ? argv[1] : "game/SonicRiders.iso";
    mem_init();
    if (!disc_open(disc)) rt_fatal("cannot open the disc image %s", disc);
    load_game();
    cpu_init(&g_main_cpu);
    sched_init();
    gx_init();
    render_start();
    dsp_init();
    {
        const char* card = getenv("SR_CARD");
        exi_init(card ? card : "saves/card_a.raw");
    }
    vi_init();
    const char* wd = getenv("SR_WATCHDOG");
    if (wd && atoi(wd) > 0) {
        g_ppc_trace = 1;
        thrd_t t;
        thrd_create(&t, watchdog, (void*)(intptr_t)atoi(wd));
    }
    rt_log("[boot] %u recompiled functions, %u native replacements; starting at %08X",
           g_recomp_func_count, g_recomp_hle_count, g_recomp_entry_point);
    g_main_cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&g_main_cpu);
    rt_log("[boot] the game's entry point returned");
    return 0;
}
