/* PS Vita platform layer (probe V1a, docs/notes/vita-research.md): paths and log, clocks, threads, the
 * guest memory window. No picture, sound or controller yet (null_vita.c).
 *
 * Files (ux0:data/sonicriders/):
 *   *.iso               the user's own disc image (GXEE8P, the first .iso/.gcm found)
 *   env.txt             KEY=VALUE lines (SR_PERFTEST=1, SR_VITA_CPU=444, ...), there being no environment
 *   saves/card_a.raw    memory card in slot A
 *   sonicriders.log     log of the last session (the previous one: sonicriders.prev.log)
 *
 * Guest memory: as on the PC, a window of the whole physical range (ppc.h) is reserved and only MEM1 (+ the
 * REL area) and the locked cache are backed. The Vita's own API commits every block it maps, so the
 * reservation goes through kubridge (kuKernelMemReserve / kuKernelMemCommit, kubridge 0.2+ loaded in
 * ur0:tai/config.txt *KERNEL). */
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <kubridge.h>

#include "runtime.h"

#define BASE_DIR "ux0:data/sonicriders"

/* newlib heap for the game's host side (ARAM copy, textures, frame lists) and the main thread's stack
 * (guest code nests deeply) */
int _newlib_heap_size_user = 192 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 8 * 1024 * 1024;

/* SR_VITA_CPU (MHz, default 444: the highest step Sony allowed; 500 needs an overclock plugin), GPU 222.
 * The clocks actually granted are logged: PSVshell or another plugin may override them. */
static void set_clocks(void) {
    const char* e = getenv("SR_VITA_CPU");
    int cpu = e ? atoi(e) : 444;
    int r1 = scePowerSetArmClockFrequency(cpu);
    int r2 = scePowerSetBusClockFrequency(222);
    int r3 = scePowerSetGpuClockFrequency(222);
    int r4 = scePowerSetGpuXbarClockFrequency(166);
    rt_log("[vita] clocks asked: CPU %d, bus 222, GPU 222, xbar 166 (rc %X %X %X %X)", cpu, r1, r2, r3, r4);
    rt_log("[vita] clocks now: CPU %d, bus %d, GPU %d, xbar %d MHz", scePowerGetArmClockFrequency(),
           scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
}

/* newlib declares C11 timespec_get but VitaSDK does not provide it; CLOCK_REALTIME is also the clock of
 * cnd_timedwait's absolute deadlines (compat_vita/threads.h) */
int timespec_get(struct timespec* ts, int base) {
    if (base != TIME_UTC || clock_gettime(CLOCK_REALTIME, ts)) return 0;
    return base;
}

void platform_set_handheld_gpu(int on) { (void)on; }

/* Start-up breadcrumbs: one line per step, written and closed at once (the log may never open), so
 * boot.txt shows over FTP how far a run got even without a picture. */
void vita_mark(const char* what) {
    SceUID fd = sceIoOpen(BASE_DIR "/boot.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0) return;
    char line[160];
    int n = snprintf(line, sizeof line, "%llu us: %s\n", (unsigned long long)sceKernelGetProcessTimeWide(), what);
    sceIoWrite(fd, line, n > 0 ? (SceSize)n : 0);
    sceIoClose(fd);
}
__attribute__((constructor)) static void vita_boot_mark(void) {
    sceIoMkdir(BASE_DIR, 0777);
    sceIoRemove(BASE_DIR "/boot.txt");
    vita_mark("process started (static constructors)");
}

void platform_init(void) {
    vita_mark("platform_init");
    mkdir(BASE_DIR, 0777);
    mkdir(BASE_DIR "/saves", 0777);
    remove(BASE_DIR "/sonicriders.prev.log");
    rename(BASE_DIR "/sonicriders.log", BASE_DIR "/sonicriders.prev.log");
    if (freopen(BASE_DIR "/sonicriders.log", "w", stderr)) {
        /* a large buffer: rt_log flushes at most once a second (per-line writes cost milliseconds here) */
        static char logbuf[64 * 1024];
        /* probe: line buffered, so a crash loses no line (costs time per line; perf runs log little) */
        setvbuf(stderr, logbuf, _IOLBF, sizeof logbuf);
    }
    rt_log("[vita] Sonic Riders native port (probe); log started");
    FILE* f = fopen(BASE_DIR "/env.txt", "r");
    if (f) {
        static char line[8192];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char* eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            setenv(line, eq + 1, 1);
            rt_log("[vita] env %s=%s", line, eq + 1);
        }
        fclose(f);
    }
    set_clocks();
    vita_mark("platform_init done (log open, clocks set)");
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

const char* platform_save_path(void) { return getenv("SR_CARD") ? getenv("SR_CARD") : BASE_DIR "/saves/card_a.raw"; }
const char* platform_settings_path(void) { return BASE_DIR "/settings.ini"; }
const char* platform_mods_dir(void) { return BASE_DIR "/mods"; }

uint64_t platform_now_ns(void) { return (uint64_t)sceKernelGetProcessTimeWide() * 1000u; }  /* microseconds */
void platform_sleep_ns(uint64_t ns) {
    SceUInt us = (SceUInt)(ns / 1000u);
    sceKernelDelayThread(us ? us : 1);
}

void platform_local_time(int* year, int* month, int* day, int* hour, int* minute, int* second) {
    SceDateTime t;
    memset(&t, 0, sizeof t);
    if (sceRtcGetCurrentClockLocalTime(&t) < 0) {
        t.year = 2000; t.month = 1; t.day = 1;
    }
    *year = t.year; *month = t.month; *day = t.day;
    *hour = t.hour; *minute = t.minute; *second = t.second;
}

/* ---- threads: pthreads (VitaSDK's pthread library only works on threads it created itself: the
 * runtime's mutexes and condition variables go through it); core masks are set by rt_thread_pin ---- */
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

static void change_priority(int delta) {
    int prio = sceKernelGetThreadCurrentPriority();
    int rc = sceKernelChangeThreadPriority(0, prio + delta);  /* lower number = runs first */
    if (rc < 0) rt_log("[vita] sceKernelChangeThreadPriority(%d) failed (0x%X)", prio + delta, rc);
}
void rt_thread_lower_priority(void) { change_priority(+1); }
void rt_thread_raise_priority(void) { change_priority(-1); }

/* cores 0-2 are the application's (core 3 is the system's) */
void rt_thread_pin(int core) {
    if (core > 2) core = 2;
    int rc = sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0 << core);
    if (rc < 0) rt_log("[vita] affinity core %d failed (0x%X)", core, rc);
}

/* ---- guest memory window (ppc_mem_base_var is defined in memory.c) ---- */
#define WINDOW_SIZE 0x20010000u
void mem_init(void) {
    vita_mark("mem_init: reserving the guest window");
    void* w = NULL;
    SceUID blk = kuKernelMemReserve(&w, WINDOW_SIZE, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW);
    if (blk < 0) rt_fatal("kuKernelMemReserve of %u MiB failed (0x%X): kubridge 0.2+ loaded?", WINDOW_SIZE >> 20, blk);
    const struct { uint32_t start, size; } r[] = {{0, GUEST_RAM_SIZE}, {LOCKED_CACHE_PHYS, 0x10000}};
    for (int i = 0; i < 2; i++) {
        int rc = kuKernelMemCommit((uint8_t*)w + r[i].start, r[i].size, KU_KERNEL_PROT_READ | KU_KERNEL_PROT_WRITE, NULL);
        if (rc < 0) rt_fatal("kuKernelMemCommit %08X (+%X) failed (0x%X)", r[i].start, r[i].size, rc);
        memset((uint8_t*)w + r[i].start, 0, r[i].size);
    }
    ppc_mem_base_var = (uint8_t*)w;
    vita_mark("mem_init done");
    rt_log("[mem] guest window at %p (kubridge reservation, %u MiB)", w, WINDOW_SIZE >> 20);
}

void profile_register_thread(void) {}
void profile_start(void) {}
void profile_dump(void) {}
void platform_sample_threads(void) {}
