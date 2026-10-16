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
 * starting a game). */
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
    if (!freopen(BASE_DIR "/sonicriders.log", "w", stderr)) return;
    setvbuf(stderr, NULL, _IOLBF, 0);
    rt_log("[switch] Sonic Riders native port; log started");
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

void mem_init(void) {
    if (!envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x73))
        rt_fatal("guest memory needs svcMapProcessCodeMemory: start the Homebrew Menu in title mode "
                 "(hold R while starting any game), not from the album");
    const uint64_t window_size = 0x20010000ull;
    Handle self = envGetOwnProcessHandle();
    virtmemLock();
    void* window = virtmemFindCodeMemory(window_size, 0x10000);
    if (!window || !virtmemAddReservation(window, window_size)) {
        virtmemUnlock();
        rt_fatal("cannot reserve the guest address window");
    }
    virtmemUnlock();
    for (size_t i = 0; i < sizeof k_ranges / sizeof k_ranges[0]; i++) {
        void* backing = aligned_alloc(0x1000, k_ranges[i].size);
        if (!backing) rt_fatal("out of memory for guest RAM");
        memset(backing, 0, k_ranges[i].size);
        uint8_t* dst = (uint8_t*)window + k_ranges[i].start;
        Result rc = svcMapProcessCodeMemory(self, (u64)dst, (u64)backing, k_ranges[i].size);
        if (R_FAILED(rc)) rt_fatal("svcMapProcessCodeMemory %08X failed (0x%X)", k_ranges[i].start, rc);
        rc = svcSetProcessMemoryPermission(self, (u64)dst, k_ranges[i].size, Perm_Rw);
        if (R_FAILED(rc)) rt_fatal("svcSetProcessMemoryPermission %08X failed (0x%X)", k_ranges[i].start, rc);
    }
    ppc_mem_base_var = (uint8_t*)window;
    rt_log("[mem] guest window at %p", window);
}
