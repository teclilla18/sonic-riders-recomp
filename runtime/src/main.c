/* Entry point: sonicriders [path to the GXEE8P disc image] (default: platform_disc_path) */
#ifdef SR_IOS
#include <SDL.h>
#endif
#include <stdio.h>
#include <sys/stat.h>
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

#if defined(__ANDROID__) || defined(SR_IOS)
#include <SDL_main.h>  /* SDLActivity (Android) / SDL's UIKit app delegate (iOS) start the program at SDL_main */
int platform_android_args(int argc, char** argv);
#endif

#ifdef SR_IOS
/* a message before the game's own window exists: UIKit shows an alert only over a visible window, so open
 * a plain one on the scene first (without it the alert waited unseen on a black screen) */
static void ios_message(const char* title, const char* text) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* w = SDL_CreateWindow("Sonic Riders", 0, 0, 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
    void ios_attach_window(SDL_Window* win);  /* ios_scene.m: onto the app's window scene, or it stays unseen */
    if (w) ios_attach_window(w);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, text, w);
    if (w) SDL_DestroyWindow(w);
}
#endif

int main(int argc, char** argv) {
#if defined(__ANDROID__) || defined(SR_IOS)
    argc = platform_android_args(argc, argv);  /* launch arguments KEY=VALUE -> environment */
#endif
    platform_init();
    rt_log("[build] %s", g_build_version);
    const char* disc = argc > 1 ? argv[1] : platform_disc_path();
    mem_init();
    settings_load();
#if !defined(__SWITCH__) && !defined(__ANDROID__) && !defined(SR_IOS)
    if (getenv("SR_BUGREPORT")) {  /* test: the menu's "Save a bug report" at start-up */
        int platform_bug_report(char* name, size_t n);
        char name[64];
        platform_bug_report(name, sizeof name);
    }
#endif
    { void netplay_init(void); netplay_init(); }
    { void update_check_start(void); update_check_start(); }  /* newer release on GitHub? (banner) */  /* SR_RECORD / SR_REPLAY / SR_NET (they turn SR_DETCLOCK on) */
#ifdef SR_IOS
    if (argc <= 1) {  /* no image yet (a Sideloadly install starts empty): say where it goes, not just close */
        FILE* f = fopen(disc, "rb");
        if (!f) {
            rt_log("[ios] no disc image in Documents or Documents/baserom");
            fflush(stderr);
            ios_message( "Sonic Riders needs your disc image",
                "Copy your own Sonic Riders (USA) disc image (.iso, 1.4 GB) into this app's folder, then open the game again.\n\n"
                "From a Mac: connect the device, open it in Finder, Files tab, and drag the .iso onto Sonic Riders.\n"
                "From a PC: the same in iTunes or the Apple Devices app (File Sharing).\n"
                "On the device: Files app > On My iPhone (iPad) > Sonic Riders.");
            exit(0);
        }
        /* opened while Finder was still copying it: the game hung at its first big read (2026-11-08) */
        long long size = fseeko(f, 0, SEEK_END) == 0 ? (long long)ftello(f) : -1;
        fclose(f);
        /* still being written (Finder may make the file full size first): size or time changes within 1.5 s */
        struct stat s1, s2;
        /* only when it changed in the last 2 minutes: a finished copy keeps the image's own (older) date */
        if (stat(disc, &s1) == 0 && time(NULL) - s1.st_mtimespec.tv_sec < 120) {
            struct timespec ts = {1, 500000000};
            thrd_sleep(&ts, NULL);
            if (stat(disc, &s2) == 0 && (s2.st_size != s1.st_size || s2.st_mtimespec.tv_sec != s1.st_mtimespec.tv_sec ||
                                         s2.st_mtimespec.tv_nsec != s1.st_mtimespec.tv_nsec)) {
                rt_log("[ios] the disc image is still being written (%lld -> %lld bytes)", (long long)s1.st_size,
                       (long long)s2.st_size);
                fflush(stderr);
                ios_message("The disc image is still copying",
                    "Your disc image is still being copied to this device.\n\n"
                    "Wait until Finder has finished (its progress circle is gone), then open the game again.");
                exit(0);
            }
        }
        if (size >= 0 && size < 1459978240LL) {
            rt_log("[ios] the disc image is only %lld bytes (a full one: 1459978240)", size);
            fflush(stderr);
            ios_message( "The disc image is incomplete",
                "Your disc image is smaller than a full Sonic Riders (USA) disc (1.4 GB).\n\n"
                "If it is still being copied, wait until Finder has finished, then open the game again. Otherwise "
                "copy a clean, full .iso again (not RVZ, CISO or a trimmed image).");
            exit(0);
        }
    }
#endif
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
        int perftest_on(void); int netplay_online_save(void);
        /* SR_PERFTEST, and online play (the generated save, docs/ONLINE.md): a fresh card in memory;
         * the player's save is left alone */
        if (perftest_on() || netplay_online_save()) exi_init(NULL);
        else exi_init(card ? card : platform_save_path());
    }
    { void netplay_connect(void); netplay_connect(); }  /* online: meet the other machine first */
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
#ifdef SR_IOS
    { void render_main_loop(void); render_main_loop(); }  /* never returns (render_vk.c) */
#endif
    for (;;) {
        struct timespec ts = {3600, 0};
        thrd_sleep(&ts, NULL);
    }
}
