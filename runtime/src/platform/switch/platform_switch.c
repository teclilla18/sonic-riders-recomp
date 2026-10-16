/* Switch platform layer: SD-card paths and log, threads, the guest memory window.
 *
 * Files (sdmc:/switch/sonicriders/):
 *   sonicriders.nro     the program
 *   *.iso               the user's own disc image (GXEE8P, the first .iso found)
 *   saves/card_a.raw    memory card in slot A (created formatted on first start)
 *   sonicriders.log     log of the last session
 *
 * Guest memory follows SwitchWakerHD (runtime/src/platform/core_switch.cpp): Horizon commits every
 * mapping, so a code-memory region is reserved for the whole physical window and only MEM1 (+ the
 * REL area) and the locked cache are backed, by mapping heap pages there. That needs
 * svcMapProcessCodeMemory, available when the Homebrew Menu runs in title mode (hold R while
 * starting a game); otherwise svcMapMemory into the stack region is used. */
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "runtime.h"

#define BASE_DIR "sdmc:/switch/sonicriders"

/* newlib declares C11 timespec_get but libnx does not provide it; CLOCK_REALTIME is also the clock
 * of cnd_timedwait's absolute deadlines */
int timespec_get(struct timespec* ts, int base) {
    if (base != TIME_UTC || clock_gettime(CLOCK_REALTIME, ts)) return 0;
    return base;
}

void platform_init(void) {
    mkdir(BASE_DIR, 0777);
    mkdir(BASE_DIR "/saves", 0777);
    /* the previous session's log is kept (a relaunch would otherwise erase it) */
    remove(BASE_DIR "/sonicriders.prev.log");
    rename(BASE_DIR "/sonicriders.log", BASE_DIR "/sonicriders.prev.log");
    if (!freopen(BASE_DIR "/sonicriders.log", "w", stderr)) return;
    setvbuf(stderr, NULL, _IOLBF, 0);
    rt_log("[switch] Sonic Riders native port; log started");
    /* debugging switches (SR_WATCHDOG, ...) as KEY=VALUE lines in env.txt, there being no environment */
    FILE* f = fopen(BASE_DIR "/env.txt", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char* eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            setenv(line, eq + 1, 1);
            rt_log("[switch] env %s=%s", line, eq + 1);
        }
        fclose(f);
    }
}

const char* platform_disc_path(void) {
    static char path[512] = BASE_DIR "/game.iso";
    DIR* d = opendir(BASE_DIR);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d))) {
            size_t n = strlen(e->d_name);
            if (n > 4 && (!strcasecmp(e->d_name + n - 4, ".iso") || !strcasecmp(e->d_name + n - 4, ".gcm"))) {
                snprintf(path, sizeof path, BASE_DIR "/%s", e->d_name);
                break;
            }
        }
        closedir(d);
    }
    return path;
}

const char* platform_save_path(void) { return BASE_DIR "/saves/card_a.raw"; }
const char* platform_settings_path(void) { return BASE_DIR "/settings.ini"; }

/* ---- threads: libnx gives threads created without attributes a 128 KiB stack ---- */
typedef struct { int (*fn)(void*); void* arg; } Start;
static void* trampoline(void* p) {
    Start s = *(Start*)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}
void rt_thread_start(int (*fn)(void*), void* arg) {
    Start* s = malloc(sizeof *s);
    s->fn = fn;
    s->arg = arg;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 4 << 20);
    pthread_t t;
    if (pthread_create(&t, &a, trampoline, s)) rt_fatal("cannot create a thread");
    pthread_attr_destroy(&a);
}

void rt_thread_pin(int core) {
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
    if (R_FAILED(rc)) rt_log("[switch] svcSetThreadCoreMask(%d) failed (0x%X)", core, rc);
}

/* ---- guest memory window (ppc_mem_base_var is defined in memory.c) ---- */

static const struct { uint32_t start, size; } k_ranges[] = {
    {0x00000000, GUEST_RAM_SIZE},              /* MEM1 + the REL area above it */
    {LOCKED_CACHE_PHYS, 0x10000},              /* the 16 KiB locked cache (mapped in 64 KiB) */
};

/* Map heap pages over [window + start, +size) for each guest range. Code memory (title mode) is
 * tried first, as on SwitchWakerHD; svcMapMemory into the stack region is the fallback (also what
 * emulators allow: they do not give homebrew its own process handle). */
static int map_ranges(uint8_t* window, int code) {
    Handle self = envGetOwnProcessHandle();
    for (size_t i = 0; i < sizeof k_ranges / sizeof k_ranges[0]; i++) {
        void* backing = aligned_alloc(0x1000, k_ranges[i].size);
        if (!backing) rt_fatal("out of memory for guest RAM");
        memset(backing, 0, k_ranges[i].size);
        uint8_t* dst = window + k_ranges[i].start;
        Result rc;
        if (code) {
            rc = svcMapProcessCodeMemory(self, (u64)dst, (u64)backing, k_ranges[i].size);
            if (R_SUCCEEDED(rc)) {
                rc = svcSetProcessMemoryPermission(self, (u64)dst, k_ranges[i].size, Perm_Rw);
                if (R_FAILED(rc)) rt_fatal("svcSetProcessMemoryPermission %08X failed (0x%X)", k_ranges[i].start, rc);
            }
        } else {
            rc = svcMapMemory(dst, backing, k_ranges[i].size);
        }
        if (R_FAILED(rc)) {
            if (code && i == 0) {
                free(backing);
                rt_log("[mem] code memory mapping failed (0x%X); using svcMapMemory", rc);
                return 0;
            }
            rt_fatal("%s %08X failed (0x%X)", code ? "svcMapProcessCodeMemory" : "svcMapMemory", k_ranges[i].start, rc);
        }
    }
    return 1;
}

static int try_window(int code) {
    const uint64_t window_size = 0x20010000ull;
    virtmemLock();
    void* window = code ? virtmemFindCodeMemory(window_size, 0x10000) : virtmemFindStack(window_size, 0x10000);
    VirtmemReservation* rv = window ? virtmemAddReservation(window, window_size) : NULL;
    virtmemUnlock();
    if (!rv) rt_fatal("cannot reserve the guest address window");
    if (!map_ranges((uint8_t*)window, code)) {
        virtmemLock();
        virtmemRemoveReservation(rv);
        virtmemUnlock();
        return 0;
    }
    ppc_mem_base_var = (uint8_t*)window;
    rt_log("[mem] guest window at %p (%s)", window, code ? "code memory" : "svcMapMemory");
    return 1;
}

void mem_init(void) {
    int code = envIsSyscallHinted(0x77) && envIsSyscallHinted(0x73) && envGetOwnProcessHandle() != INVALID_HANDLE;
    if (!(code && try_window(1)) && !try_window(0)) rt_fatal("cannot map guest memory");
}
