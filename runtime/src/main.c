/* Entry point: sonicriders [path to the GXEE8P disc image] (default: platform_disc_path) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* SR_WATCH=ADDR,VALUE (hex): a spare thread spins on a guest word and, the moment it holds VALUE
 * (SR_WATCH=ADDR,~VALUE: the moment it stops holding VALUE), dumps the function trace (with
 * SR_WATCHDOG on) and the threads: finds who overwrites memory */
static uint32_t g_watch_addr, g_watch_val, g_watch_more[3];
static int g_watch_leave;
static int watch_thread(void* arg) {
    (void)arg;
    rt_thread_pin(3);
    uint32_t last = mem_r32(g_watch_addr);
    rt_log("[watch] %08X = %08X, waiting for %08X", g_watch_addr, last, g_watch_val);
    for (;;) {
        uint32_t v = mem_r32(g_watch_addr);
        if (g_watch_leave == 2) {  /* SR_WATCH=ADDR,*[,ADDR2,ADDR3,ADDR4]: log every change */
            static uint32_t prev[3];
            if (v != last) {
                rt_log("[watch] %08X: %08X -> %08X (field %u)", g_watch_addr, last, v, vi_field_count());
                platform_sample_threads();
            }
            last = v;
            for (int k = 0; k < 3; k++) {
                if (!g_watch_more[k]) continue;
                uint32_t u = mem_r32(g_watch_more[k]);
                if (u != prev[k]) rt_log("[watch] %08X: %08X -> %08X (field %u)", g_watch_more[k], prev[k], u, vi_field_count());
                prev[k] = u;
            }
            continue;
        }
        if (v != last && (g_watch_leave ? last == g_watch_val : v == g_watch_val)) {
            platform_sample_threads();
            rt_log("[watch] %08X changed %08X -> %08X", g_watch_addr, last, v);
            for (uint32_t a = (g_watch_addr & ~31u) - 0x40; a < (g_watch_addr & ~31u) + 0x60; a += 32)
                rt_log("[watch]   %08X: %08X %08X %08X %08X %08X %08X %08X %08X", a, mem_r32(a), mem_r32(a + 4), mem_r32(a + 8),
                       mem_r32(a + 12), mem_r32(a + 16), mem_r32(a + 20), mem_r32(a + 24), mem_r32(a + 28));
            cpu_dump_trace();
            sched_dump();
            fflush(stderr);
            return 0;
        }
        last = v;
    }
}

static int game_thread(void* arg) {
    (void)arg;
    rt_thread_pin(0);
    profile_register_thread();
    sched_enter_boot_thread();
    ppc_enter_guest_thread();  /* the Switch's generated code reads the memory base from x28 */
    g_main_cpu.pc = g_recomp_entry_point;
    ppc_dispatch(&g_main_cpu);
    rt_fatal("the game's entry point returned");
}

extern const char g_build_version[]; /* build/<target>/gen/version.c, written by configure.py */

int main(int argc, char** argv) {
    platform_init();
    rt_log("[build] %s", g_build_version);
    const char* disc = argc > 1 ? argv[1] : platform_disc_path();
    mem_init();
    settings_load();
    { void netplay_init(void); netplay_init(); }  /* SR_RECORD / SR_REPLAY / SR_NET (they turn SR_DETCLOCK on) */
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
    { void netplay_connect(void); netplay_connect(); }  /* online: meet the other machine (same save) first */
    vi_init();
    const char* wd = getenv("SR_WATCHDOG");
    if (wd && atoi(wd) > 0) {
        g_ppc_trace = !getenv("SR_WATCHDOG_NOTRACE");  /* the trace slows the game: may hide races */
        rt_thread_start(watchdog, (void*)(intptr_t)atoi(wd));
    }
    const char* w = getenv("SR_WATCH");
    if (w && sscanf(w, "%x,", &g_watch_addr) == 1 && strchr(w, ',')) {
        const char* v = strchr(w, ',') + 1;
        g_watch_leave = *v == '~' ? 1 : *v == '*' ? 2 : 0;
        g_watch_val = (uint32_t)strtoul(v + (g_watch_leave != 0), NULL, 16);
        const char* m = strchr(v, ',');
        for (int k = 0; k < 3 && m; k++, m = strchr(m + 1, ',')) g_watch_more[k] = (uint32_t)strtoul(m + 1, NULL, 16);
        rt_thread_start(watch_thread, NULL);
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
