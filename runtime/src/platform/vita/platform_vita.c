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

#include <psp2/kernel/clib.h>
#include <psp2/kernel/dmac.h>
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

/* GCC's emulated TLS (libgcc emutls, used for _Thread_local here) treats the program as single-threaded,
 * one copy shared by every thread, unless the weak symbol pthread_cancel resolves (its "threads active"
 * test); nothing else here calls it, so it would not be linked. Referencing it, and the other pthread
 * calls emutls makes through weak references, gives each thread its own copy (tls_check logs it). */
__attribute__((used)) static void* const k_emutls_needs[] = {
    (void*)pthread_cancel, (void*)pthread_once, (void*)pthread_getspecific, (void*)pthread_setspecific,
    (void*)pthread_key_create, (void*)pthread_mutex_init, (void*)pthread_mutex_lock, (void*)pthread_mutex_unlock,
};

/* _Thread_local is emulated (GCC emutls over pthread keys) here; the scheduler's t_self must differ per
 * host thread or guest thread handoffs deadlock. Checked once at start. */
static _Thread_local int t_tls_probe;
static pthread_key_t g_probe_key;
static void* g_probe_key_seen = (void*)1;
static int g_probe_tid_main, g_probe_tid_new;
static void* tls_probe_thread(void* arg) {
    g_probe_key_seen = pthread_getspecific(g_probe_key);  /* must be NULL in a new thread */
    g_probe_tid_new = sceKernelGetThreadId();
    *(int*)arg = t_tls_probe;  /* a new thread must see 0, not the main thread's 1 */
    t_tls_probe = 2;
    return NULL;
}
static void tls_check(void) {
    pthread_key_create(&g_probe_key, NULL);
    pthread_setspecific(g_probe_key, &g_probe_key);
    g_probe_tid_main = sceKernelGetThreadId();
    t_tls_probe = 1;
    int seen = -1;
    pthread_t t;
    if (pthread_create(&t, NULL, tls_probe_thread, &seen) == 0) pthread_join(t, NULL);
    rt_log("[vita] thread-local check: new thread saw %d, main thread keeps %d (%s)", seen, t_tls_probe,
           seen == 0 && t_tls_probe == 1 ? "per thread: ok" : "SHARED: guest thread switches will hang");
    rt_log("[vita] pthread key: new thread got %p (main set %p; NULL expected); thread ids %X / %X", g_probe_key_seen,
           (void*)&g_probe_key, g_probe_tid_main, g_probe_tid_new);
}

/* what the clocks and the emulated thread-locals cost (the renderer's [perf] timers read the clock
 * ~10 times per draw) */
static void clock_bench(void) {
    enum { N = 100000 };
    volatile uint64_t sink = 0;
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    for (int i = 0; i < N; i++) sink += sceKernelGetProcessTimeWide();
    SceUInt64 t1 = sceKernelGetProcessTimeWide();
    for (int i = 0; i < N; i++) sink += sceKernelGetSystemTimeWide();
    SceUInt64 t2 = sceKernelGetProcessTimeWide();
    for (int i = 0; i < N; i++) sink += rt_ticks();
    SceUInt64 t3 = sceKernelGetProcessTimeWide();
    for (int i = 0; i < N; i++) { t_tls_probe++; __asm__ __volatile__("" ::: "memory"); }
    SceUInt64 t4 = sceKernelGetProcessTimeWide();
    for (int i = 0; i < N; i++) sink += platform_now_ns() / 2000u;
    SceUInt64 t5 = sceKernelGetProcessTimeWide();
    (void)sink;
    rt_log("[vita] cost per call (ns): sceKernelGetProcessTimeWide %.0f, sceKernelGetSystemTimeWide %.0f, rt_ticks %.0f, "
           "thread-local increment %.0f, clock + 64-bit division %.0f", (t1 - t0) * 1e3 / N, (t2 - t1) * 1e3 / N,
           (t3 - t2) * 1e3 / N, (t4 - t3) * 1e3 / N, (t5 - t4) * 1e3 / N);
    /* block copies (ARAM DMA moves ~2 MB per race frame): newlib's memcpy, the system's, the DMA engine */
    enum { SZ = 2 << 20, REP = 8 };
    uint8_t* a = malloc(SZ);
    uint8_t* b = malloc(SZ);
    if (!a || !b) { free(a); free(b); return; }
    for (int i = 0; i < SZ; i++) a[i] = (uint8_t)(i * 7 + (i >> 11));
    double mbs[3];
    int ok[3];
    for (int k = 0; k < 3; k++) {
        memset(b, 0, SZ);
        SceUInt64 c0 = sceKernelGetProcessTimeWide();
        for (int r = 0; r < REP; r++) {
            if (k == 0) memcpy(b, a, SZ);
            else if (k == 1) sceClibMemcpy(b, a, SZ);
            else sceDmacMemcpy(b, a, SZ);
        }
        SceUInt64 c1 = sceKernelGetProcessTimeWide();
        mbs[k] = (double)SZ * REP / (double)(c1 - c0 + 1);  /* bytes per us = MB/s */
        ok[k] = memcmp(a, b, SZ) == 0;
    }
    rt_log("[vita] copy 2 MB: memcpy %.0f MB/s%s, sceClibMemcpy %.0f MB/s%s, sceDmacMemcpy %.0f MB/s%s", mbs[0],
           ok[0] ? "" : " (WRONG)", mbs[1], ok[1] ? "" : " (WRONG)", mbs[2], ok[2] ? "" : " (WRONG)");
    free(a);
    free(b);
}

void platform_init(void) {
    vita_mark("platform_init");
    mkdir(BASE_DIR, 0777);
    mkdir(BASE_DIR "/saves", 0777);
    remove(BASE_DIR "/sonicriders.prev.log");
    rename(BASE_DIR "/sonicriders.log", BASE_DIR "/sonicriders.prev.log");
    if (freopen(BASE_DIR "/sonicriders.log", "w", stderr)) {
        /* a large buffer: rt_log flushes at most once a second (per-line writes cost milliseconds here);
         * SR_LOGSYNC=1 in env.txt flushes every line (log.c), boot.txt keeps the start-up steps */
        static char logbuf[64 * 1024];
        setvbuf(stderr, logbuf, _IOFBF, sizeof logbuf);
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
    tls_check();
    clock_bench();
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
uint64_t platform_now_us(void) { return sceKernelGetProcessTimeWide(); }  /* rt_ticks (cpu.c) */
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
/* stall watch (as on the Switch): no new frame for 5 s -> where every guest thread and device is, once
 * per stall; also a once-a-minute line with the frame count while booting slowly */
void sched_dump(void);
void mmio_dump(void);
unsigned long long gx_frames_presented(void);
static int stall_thread(void* arg) {
    (void)arg;
    rt_thread_pin(2);
    unsigned long long last = gx_frames_presented();
    unsigned still = 0, ticks = 0;
    int reported = 0;
    for (;;) {
        sceKernelDelayThread(1000000);
        unsigned long long f = gx_frames_presented();
        if (++ticks % 10 == 0) {
            extern unsigned g_rt_ticks_reads;
            static unsigned reads0;
            unsigned r = g_rt_ticks_reads;
            rt_log("[vita] %u s: %llu frames presented, retrace %u, clock reads %u/s", ticks, f, vi_field_count(), (r - reads0) / 10);
            reads0 = r;
        }
        if (f != last) {
            if (reported) rt_log("[stall] frames again after %u s", still);
            last = f; still = 0; reported = 0;
            continue;
        }
        if (++still == 5 && !reported) {
            reported = 1;
            rt_log("[stall] no new frame for 5 s (frame %llu, retrace %u): state follows", f, vi_field_count());
            mmio_dump();
            sched_dump();
            fflush(stderr);
            if (getenv("SR_VITA_STALLCORE")) {  /* a core dump with every thread's registers (vita-research.md) */
                rt_log("[stall] SR_VITA_STALLCORE: stopping with a crash dump");
                fflush(stderr);
                *(volatile int*)0 = 0;
            }
        }
    }
    return 0;
}
/* Sampling profiler (SR_PROFILE=1 in env.txt), as on the Switch (platform_switch.c): a thread on core 1
 * reads g_ppc_cur_fn (the guest function entered last on the game thread, or a PROF_* tag for runtime
 * work) every millisecond and logs the 40 most frequent every 10 s (SR_PROFILE_SECS). A sample is charged
 * to the last function entered, so a caller's own code after a call returns counts for the callee. */
#define PROF_BUCKETS 16384u
static struct { uint32_t fn, lr, count; } g_prof[PROF_BUCKETS];
static uint32_t g_prof_samples, g_prof_overflow;

static void prof_report(unsigned secs) {
    uint32_t n = g_prof_samples;
    if (!n) return;
    rt_log("[profile] %u samples over the last %u s: last function entered on the game thread, and its return address:", n, secs);
    uint32_t tags[5] = {0};
    for (uint32_t i = 0; i < PROF_BUCKETS; i++)
        if (g_prof[i].count && g_prof[i].fn >= PROF_IDLE && g_prof[i].fn <= PROF_RESUMED) tags[g_prof[i].fn - PROF_IDLE] += g_prof[i].count;
    rt_log("[profile]   tags: idle %.1f%%, draw %.1f%%, GX FIFO %.1f%%, thread switch %.1f%%, resumed %.1f%%", 100.0 * tags[0] / n,
           100.0 * tags[1] / n, 100.0 * tags[2] / n, 100.0 * tags[3] / n, 100.0 * tags[4] / n);
    for (int k = 0; k < 40; k++) {
        uint32_t best = 0, bi = 0;
        for (uint32_t i = 0; i < PROF_BUCKETS; i++)
            if (g_prof[i].count > best) { best = g_prof[i].count; bi = i; }
        if (!best) break;
        uint32_t fn = g_prof[bi].fn, lr = g_prof[bi].lr;
        if (fn >= PROF_IDLE) rt_log("[profile]   %5.1f%%  %08X (tag)", 100.0 * best / n, fn);
        else rt_log("[profile]   %5.1f%%  %08X  from %08X", 100.0 * best / n, fn, lr);
        g_prof[bi].count = 0;
    }
    if (g_prof_overflow) rt_log("[profile]   (%u samples lost: table full)", g_prof_overflow);
    memset(g_prof, 0, sizeof g_prof);
    g_prof_samples = g_prof_overflow = 0;
}

static int prof_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);
    rt_thread_raise_priority();
    rt_thread_raise_priority();
    const char* e = getenv("SR_PROFILE_SECS");
    unsigned secs = e && atoi(e) > 0 ? (unsigned)atoi(e) : 10;
    SceUInt64 next_report = sceKernelGetProcessTimeWide() + secs * 1000000ull;
    for (;;) {
        sceKernelDelayThread(1000);
        uint32_t fn = g_ppc_cur_fn, lr = fn >= 0xF0000000u ? 0 : g_ppc_cur_lr;
        uint32_t h = ((fn ^ (lr * 31u)) * 2654435761u) >> 18;
        for (uint32_t probe = 0;; probe++) {
            uint32_t i = (h + probe) & (PROF_BUCKETS - 1);
            if (g_prof[i].fn == fn && g_prof[i].lr == lr && g_prof[i].count) { g_prof[i].count++; break; }
            if (!g_prof[i].count) { g_prof[i].fn = fn; g_prof[i].lr = lr; g_prof[i].count = 1; break; }
            if (probe == 64) { g_prof_overflow++; break; }
        }
        g_prof_samples++;
        if (sceKernelGetProcessTimeWide() >= next_report) {
            prof_report(secs);
            next_report = sceKernelGetProcessTimeWide() + secs * 1000000ull;
        }
    }
    return 0;
}

void profile_start(void) {
    rt_thread_start(stall_thread, NULL);
    if (!getenv("SR_PROFILE")) return;
    rt_log("[profile] sampling the game thread every 1 ms (SR_PROFILE)");
    rt_thread_start(prof_thread, NULL);
}
void profile_dump(void) {}
void platform_sample_threads(void) {}
