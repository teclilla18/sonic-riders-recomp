/* Entry point: sonicriders [path to the GXEE8P disc image] (default: platform_disc_path) */
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "gx.h"
#include "gx_render.h"
#include "recomp_table.h"
#include "runtime.h"
#include "settings.h"

static Cpu g_main_cpu;

extern int g_ppc_trace;
void cpu_dump_trace(void);
void mmio_dump(void);
void sched_dump(void);

/* SR_WATCHDOG=N: record function entries and, after N seconds, dump where the game is and exit
 * (SR_WATCHDOG_NOTRACE=1: without the function-entry record) */
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

static int game_thread(void* arg) {
    (void)arg;
    rt_thread_pin(0);
    profile_register_thread();
    sched_enter_boot_thread();
    g_main_cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&g_main_cpu);
    rt_fatal("the game's entry point returned");
}

int main(int argc, char** argv) {
    platform_init();
    const char* disc = argc > 1 ? argv[1] : platform_disc_path();
    mem_init();
    settings_load();
    if (!disc_open(disc)) rt_fatal("cannot open the disc image %s", disc);
    load_game();
    cpu_init(&g_main_cpu);
    sched_init();
    gx_init();
    render_start();
    dsp_init();
    di_init();
    {
        const char* card = getenv("SR_CARD");
        exi_init(card ? card : platform_save_path());
    }
    vi_init();
    const char* wd = getenv("SR_WATCHDOG");
    if (wd && atoi(wd) > 0) {
        g_ppc_trace = !getenv("SR_WATCHDOG_NOTRACE");  /* the trace slows the game: may hide races */
        rt_thread_start(watchdog, (void*)(intptr_t)atoi(wd));
    }
    rt_log("[boot] %u recompiled functions, %u native replacements; starting at %08X",
           g_recomp_func_count, g_recomp_hle_count, g_recomp_entry_point);
    /* the game's boot code runs on a thread of our own (a large stack: guest calls are host calls);
     * this thread only waits */
    profile_start();
    rt_thread_start(game_thread, NULL);
    for (;;) {
        struct timespec ts = {3600, 0};
        thrd_sleep(&ts, NULL);
    }
}
