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
    static char logbuf[64 * 1024];
    setvbuf(stderr, logbuf, _IOFBF, sizeof logbuf);  /* flushed by rt_log at most once a second */
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

uint64_t platform_now_ns(void) { return armTicksToNs(armGetSystemTick()); }  /* the CPU's tick counter */

/* the console's local clock (time service), for the GameCube RTC */
void platform_local_time(int* year, int* month, int* day, int* hour, int* minute, int* second) {
    u64 now = 0;
    TimeCalendarTime ct = {0};
    TimeCalendarAdditionalInfo info;
    if (R_FAILED(timeGetCurrentTime(TimeType_Default, &now)) || R_FAILED(timeToCalendarTimeWithMyRule(now, &ct, &info))) {
        static int warned;
        if (!warned++) rt_log("[switch] the time service failed: the game's clock starts at 2000-01-01");
        ct.year = 2000; ct.month = 1; ct.day = 1;
    }
    *year = ct.year; *month = ct.month; *day = ct.day;
    *hour = ct.hour; *minute = ct.minute; *second = ct.second;
}

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

/* Sampling profiler (SR_PROFILE=1 in env.txt): a thread on core 2 reads g_ppc_cur_fn, the guest
 * function entered last on the game core or a PROF_* tag for runtime work, every millisecond, and
 * logs the 30 most frequent every 10 seconds. A sample is charged to the last function entered, so a
 * caller's own code after a call returns counts for the callee: a leaf-heavy approximation, enough
 * to see where the time goes on the hardware (the desktop build samples real instruction
 * pointers, platform/host/profile_win.c). */
#define PROF_BUCKETS 16384u
static struct { uint32_t fn, lr, count; } g_prof[PROF_BUCKETS];
static uint32_t g_prof_samples, g_prof_overflow;

static void prof_report(void) {
    uint32_t n = g_prof_samples;
    if (!n) return;
    rt_log("[profile] %u samples (1 ms) over the last 10 s: last function entered on the game core, and its return address:", n);
    for (int k = 0; k < 30; k++) {
        uint32_t best = 0, bi = 0;
        for (uint32_t i = 0; i < PROF_BUCKETS; i++)
            if (g_prof[i].count > best) { best = g_prof[i].count; bi = i; }
        if (!best) break;
        uint32_t fn = g_prof[bi].fn, lr = g_prof[bi].lr;
        const char* tag = fn == PROF_IDLE ? " (idle)" : fn == PROF_DRAW ? " (draw processing)" : fn == PROF_PIPE ? " (GX FIFO)" : fn == PROF_SWITCH ? " (thread switch)" : "";
        if (*tag) rt_log("[profile]   %5.1f%%  %08X%s", 100.0 * best / n, fn, tag);
        else rt_log("[profile]   %5.1f%%  %08X  from %08X", 100.0 * best / n, fn, lr);
        g_prof[bi].count = 0;
    }
    if (g_prof_overflow) rt_log("[profile]   (%u samples lost: table full)", g_prof_overflow);
    memset(g_prof, 0, sizeof g_prof);
    g_prof_samples = g_prof_overflow = 0;
}

static int prof_thread(void* arg) {
    (void)arg;
    rt_thread_pin(2);
    uint64_t next_report = armGetSystemTick() + armNsToTicks(10000000000ull);
    for (;;) {
        svcSleepThread(1000000);
        uint32_t fn = g_ppc_cur_fn, lr = fn >= 0xF0000000u ? 0 : g_ppc_cur_lr;
        uint32_t h = ((fn ^ (lr * 31u)) * 2654435761u) >> 18;
        for (uint32_t probe = 0;; probe++) {
            uint32_t i = (h + probe) & (PROF_BUCKETS - 1);
            if (g_prof[i].fn == fn && g_prof[i].lr == lr && g_prof[i].count) { g_prof[i].count++; break; }
            if (!g_prof[i].count) { g_prof[i].fn = fn; g_prof[i].lr = lr; g_prof[i].count = 1; break; }
            if (probe == 64) { g_prof_overflow++; break; }
        }
        g_prof_samples++;
        if (armGetSystemTick() >= next_report) {
            prof_report();
            next_report = armGetSystemTick() + armNsToTicks(10000000000ull);
        }
    }
    return 0;
}

void profile_register_thread(void) {}
void profile_start(void) {
    if (!getenv("SR_PROFILE")) return;
    rt_log("[profile] sampling the game core every 1 ms (SR_PROFILE); reports every 10 s");
    rt_thread_start(prof_thread, NULL);
}
void profile_dump(void) {}
