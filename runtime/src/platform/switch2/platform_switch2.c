/* Native kaiHEN/libounce platform. Uses the stack SVC mapping verified by
 * SR2Diag48b77e581423 on physical firmware23.0.1. No power-profile requests.
 * Thread/driver cleanup finishes before libc closes stdio. A separate FsFile
 * records userAppExit without relying on an already-closed FILE pointer. */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include "runtime.h"
#include "switch2_platform.h"

#define BASE_DIR "sdmc:/switch/sonicriders-switch2"
#define LIFE_DIR "/switch/sonicriders-switch2"
#define WORKERS 128
typedef struct {
    Thread thread;
    int (*fn)(void*);
    void* arg;
    _Atomic int exited;
    _Atomic unsigned wait_kind; /* 0=running/other, 1=sleep, 2=condition */
    _Atomic uintptr_t wait_object;
    _Atomic uint64_t wait_since;
    _Atomic unsigned wait_count;
    _Atomic uint64_t wait_max_ns;
    _Atomic unsigned wait_timeouts;
} Worker;
static Worker* g_workers[WORKERS];
static unsigned g_nworkers;
static Mutex g_workers_lock;
static _Thread_local Worker* g_self;
static _Atomic int g_stop;
static u64 g_core_mask;
static AppletHookCookie g_hook;
static FsFileSystem g_life_fs;
static FsFile g_life;
static int g_life_open;
static s64 g_life_offset;
_Atomic int g_switch_log_input;

static void life_write(const char* s) {
    if (!g_life_open) return;
    size_t n = strlen(s);
    if (R_SUCCEEDED(fsFileWrite(&g_life, g_life_offset, s, n, FsWriteOption_Flush))) g_life_offset += (s64)n;
}

void userAppInit(void) {
    mkdir("sdmc:/switch", 0777);
    mkdir(BASE_DIR, 0777);
    if (R_FAILED(fsOpenSdCardFileSystem(&g_life_fs))) return;
    for (unsigned run = 1; run < 1000000; run++) {
        char path[128];
        snprintf(path, sizeof path, LIFE_DIR "/lifecycle-%06u.log", run);
        FsFile old;
        if (R_SUCCEEDED(fsFsOpenFile(&g_life_fs, path, FsOpenMode_Read, &old))) { fsFileClose(&old); continue; }
        if (R_SUCCEEDED(fsFsCreateFile(&g_life_fs, path, 0, 0)) &&
            R_SUCCEEDED(fsFsOpenFile(&g_life_fs, path, FsOpenMode_Write | FsOpenMode_Append, &g_life))) g_life_open = 1;
        break;
    }
    if (!g_life_open) fsFsClose(&g_life_fs);
    life_write("STARTUP userAppInit reached after SDK bootstrap\n");
}

void userAppExit(void) {
    /* No stdio here: exit() may already have closed its streams. */
    life_write("EXIT_HOOK userAppExit reached; loader return still needs tester confirmation\n");
    if (g_life_open) { fsFileClose(&g_life); fsFsClose(&g_life_fs); g_life_open = 0; }
}

void switch2_checkpoint(const char* message) {
    rt_log("[switch2] %s", message);
    switch2_log_flush();
    fsync(fileno(stderr));
    life_write(message);
    life_write("\n");
}

static void applet_hook(AppletHookType type, void* arg) {
    (void)arg;
    rt_log("[switch2] applet event=%d focus=%d operation_mode=%d", type, appletGetFocusState(), appletGetOperationMode());
    switch2_log_flush();
}

int timespec_get(struct timespec* ts, int base) {
    return base == TIME_UTC && clock_gettime(CLOCK_REALTIME, ts) == 0 ? base : 0;
}

int platform_restart_online(void) { return 0; } /* use the dedicated Online NRO */

void platform_init(void) {
    remove(BASE_DIR "/sonicriders.prev.log");
    rename(BASE_DIR "/sonicriders.log", BASE_DIR "/sonicriders.prev.log");
    if (!freopen(BASE_DIR "/sonicriders.log", "w", stderr)) abort();
    static char logbuf[64 * 1024];
    setvbuf(stderr, logbuf, _IOFBF, sizeof logbuf);
    mkdir(BASE_DIR "/saves", 0777);
    FILE* f = fopen(BASE_DIR "/env.txt", "r");
    if (f) {
        char line[8192];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char* eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            if (!strncmp(line, "SR_", 3) || !strncmp(line, "MESA_", 5)) setenv(line, eq + 1, 1);
        }
        fclose(f);
    }
    Result rc = svcGetInfo(&g_core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc) || !g_core_mask) rt_fatal("[switch2] cannot query permitted cores: 0x%X", rc);
    appletHook(&g_hook, applet_hook, NULL);
    rt_log("[switch2] core mask=0x%llX operation mode=%d; system power policy unchanged", (unsigned long long)g_core_mask, appletGetOperationMode());
    rt_log("[switch2] process handle=0x%X code-map hints map=%d unmap=%d permission=%d; using tested stack SVC path",
           envGetOwnProcessHandle(), envIsSyscallHinted(0x77), envIsSyscallHinted(0x78), envIsSyscallHinted(0x73));
    switch2_checkpoint("PLATFORM_INIT_COMPLETE");
}

const char* platform_disc_path(void) {
    const char* override = getenv("SR_DISC");
    if (override && *override) return override;
    static char path[512] = BASE_DIR "/game.iso";
    DIR* d = opendir(BASE_DIR);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d))) {
            size_t n = strlen(e->d_name);
            if (n > 4 && (!strcasecmp(e->d_name + n - 4, ".iso") || !strcasecmp(e->d_name + n - 4, ".gcm"))) {
                snprintf(path, sizeof path, BASE_DIR "/%s", e->d_name); break;
            }
        }
        closedir(d);
    }
    return path;
}
const char* platform_save_path(void) { return getenv("SR_CARD") ? getenv("SR_CARD") : BASE_DIR "/saves/card_a.raw"; }
const char* platform_settings_path(void) { return BASE_DIR "/settings.ini"; }
const char* platform_mods_dir(void) { return BASE_DIR "/mods"; }
uint64_t platform_now_ns(void) { return armTicksToNs(armGetSystemTick()); }
int switch2_stopping(void) { return atomic_load_explicit(&g_stop, memory_order_acquire); }

void switch2_thread_checkpoint(void) {
    if (g_self && switch2_stopping()) {
        atomic_store_explicit(&g_self->exited, 1, memory_order_release);
        threadExit();
    }
}
static void worker_wait(unsigned kind, const void* object) {
    if (!g_self) return;
    if (kind) {
        atomic_store(&g_self->wait_object, (uintptr_t)object);
        atomic_store(&g_self->wait_since, platform_now_ns());
        atomic_fetch_add(&g_self->wait_count, 1);
    }
    atomic_store(&g_self->wait_kind, kind);
}
void platform_sleep_ns(uint64_t ns) {
    worker_wait(1, NULL);
    while (ns) {
        switch2_thread_checkpoint();
        uint64_t slice = ns < 10000000 ? ns : 10000000;
        svcSleepThread((s64)slice);
        ns -= slice;
    }
    worker_wait(0, NULL);
    switch2_thread_checkpoint();
}
void platform_local_time(int* y, int* m, int* d, int* h, int* min, int* s) {
    u64 now = 0;
    TimeCalendarTime ct = {.year = 2000, .month = 1, .day = 1};
    TimeCalendarAdditionalInfo extra;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &now))) timeToCalendarTimeWithMyRule(now, &ct, &extra);
    *y = ct.year; *m = ct.month; *d = ct.day; *h = ct.hour; *min = ct.minute; *s = ct.second;
}
void platform_set_handheld_gpu(int on) { (void)on; } /* never apply Switch 1 APM IDs */

static unsigned physical_core(int logical) {
    unsigned count = (unsigned)__builtin_popcountll(g_core_mask);
    unsigned wanted = (unsigned)(logical < 0 ? 0 : logical) % count;
    for (unsigned c = 0; c < 64; c++) if (g_core_mask & (1ULL << c)) { if (!wanted--) return c; }
    return 0;
}
void rt_thread_pin(int logical) {
    unsigned core = physical_core(logical);
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, (s32)core, 1ULL << core);
    if (R_FAILED(rc)) rt_log("[switch2] pin logical%d -> core%u failed: 0x%X", logical, core, rc);
}
static void priority_change(int delta) {
    s32 priority = 0x2c;
    u64 permitted = 0;
    if (R_FAILED(svcGetThreadPriority(&priority, CUR_THREAD_HANDLE))) return;
    int want = priority + delta;
    if (want < 0 || want > 63) return;
    if (R_SUCCEEDED(svcGetInfo(&permitted, InfoType_PriorityMask, CUR_PROCESS_HANDLE, 0)) && !(permitted & (1ULL << want))) return;
    Result rc = svcSetThreadPriority(CUR_THREAD_HANDLE, want);
    if (R_FAILED(rc)) rt_log("[switch2] priority%d failed: 0x%X", want, rc);
}
void rt_thread_lower_priority(void) { priority_change(1); }
void rt_thread_raise_priority(void) { priority_change(-1); }
static void worker_entry(void* arg) {
    g_self = arg;
    g_self->fn(g_self->arg);
    atomic_store_explicit(&g_self->exited, 1, memory_order_release);
}
void rt_thread_start(int (*fn)(void*), void* arg) {
    switch2_thread_checkpoint();
    mutexLock(&g_workers_lock);
    if (switch2_stopping()) { mutexUnlock(&g_workers_lock); return; }
    unsigned slot = g_nworkers;
    for (unsigned i = 0; i < g_nworkers; i++) {
        Worker* old = g_workers[i];
        if (old && atomic_load_explicit(&old->exited, memory_order_acquire) &&
            R_SUCCEEDED(svcWaitSynchronizationSingle(old->thread.handle, 0)) && R_SUCCEEDED(threadClose(&old->thread))) {
            free(old); g_workers[i] = NULL;
        }
        if (!g_workers[i] && slot == g_nworkers) slot = i;
    }
    if (slot == WORKERS) { mutexUnlock(&g_workers_lock); rt_fatal("[switch2] worker registry full"); }
    Worker* w = calloc(1, sizeof *w);
    if (!w) { mutexUnlock(&g_workers_lock); rt_fatal("[switch2] worker allocation failed"); }
    w->fn = fn; w->arg = arg;
    /* Helpers start away from the guest's core; individual runtime roles can pin themselves. */
    Result rc = threadCreate(&w->thread, worker_entry, w, NULL, 4 << 20, 0x2c, (int)physical_core(2));
    if (R_FAILED(rc)) { mutexUnlock(&g_workers_lock); free(w); rt_fatal("[switch2] threadCreate failed 0x%X", rc); }
    g_workers[slot] = w;
    if (slot == g_nworkers) g_nworkers++;
    rc = threadStart(&w->thread);
    mutexUnlock(&g_workers_lock);
    if (R_FAILED(rc)) rt_fatal("[switch2] threadStart failed 0x%X", rc);
}

/* Condvar waits are bounded only on this target. Cancellation happens with
 * their mutex held; release it before ending the worker to avoid orphan locks. */
int __real_pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*);
int __real_pthread_cond_timedwait(pthread_cond_t*, pthread_mutex_t*, const struct timespec*);
static int wait_cond(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec* deadline) {
    /* Keep driver-owned threads and their condvar clock attributes untouched. */
    if (!g_self) return deadline ? __real_pthread_cond_timedwait(cond, mutex, deadline) : __real_pthread_cond_wait(cond, mutex);
    for (;;) {
        if (g_self && switch2_stopping()) { pthread_mutex_unlock(mutex); switch2_thread_checkpoint(); }
        struct timespec slice;
        clock_gettime(CLOCK_REALTIME, &slice);
        slice.tv_nsec += 50000000;
        if (slice.tv_nsec >= 1000000000) { slice.tv_sec++; slice.tv_nsec -= 1000000000; }
        int final = deadline && (deadline->tv_sec < slice.tv_sec || (deadline->tv_sec == slice.tv_sec && deadline->tv_nsec <= slice.tv_nsec));
        if (final) slice = *deadline;
        /* Preserve original logical wait age across the 50ms shutdown slices. */
        if (atomic_load(&g_self->wait_kind) != 2) worker_wait(2, cond);
        int rc = __real_pthread_cond_timedwait(cond, mutex, &slice);
        uint64_t waited = platform_now_ns() - atomic_load(&g_self->wait_since);
        if (waited > atomic_load(&g_self->wait_max_ns)) atomic_store(&g_self->wait_max_ns, waited);
        if (rc == ETIMEDOUT) atomic_fetch_add(&g_self->wait_timeouts, 1);
        if (g_self && switch2_stopping()) { pthread_mutex_unlock(mutex); switch2_thread_checkpoint(); }
        /* A timed wait can consume a signal racing its timeout while it
         * reacquires the mutex. Never hide that timeout from an indefinite
         * wait's predicate loop: the selected guest may already be runnable.
         * pthread/cnd waits permit spurious wakes, with the mutex still held.
         * Explicit timed waits retain their original caller deadline. */
        if (rc == ETIMEDOUT && !deadline) { worker_wait(0, NULL); return 0; }
        if (rc != ETIMEDOUT || final) { worker_wait(0, NULL); return rc; }
    }
}
int __wrap_pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) { return wait_cond(c, m, NULL); }
int __wrap_pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t) { return wait_cond(c, m, t); }
int __wrap_thrd_sleep(const struct timespec* duration, struct timespec* remaining) {
    platform_sleep_ns((uint64_t)duration->tv_sec * 1000000000ULL + duration->tv_nsec);
    if (remaining) memset(remaining, 0, sizeof *remaining);
    return 0;
}
void switch2_stop_workers(void) {
    switch2_checkpoint("BEGIN_STOP_WORKERS");
    mutexLock(&g_workers_lock);
    atomic_store_explicit(&g_stop, 1, memory_order_release);
    unsigned count = g_nworkers;
    mutexUnlock(&g_workers_lock);
    g_core_preempt[0] = 1; /* generated entries/back edges reach ppc_preempt */
    uint64_t deadline = platform_now_ns() + 10000000000ULL;
    for (unsigned i = 0; i < count; i++) {
        Worker* w = g_workers[i];
        if (!w) continue;
        rt_log("[switch2] joining worker%u/%u", i, count);
        switch2_log_flush();
        Result rc;
        for (;;) {
            /* An IRQ already being delivered can clear the polling flag.
             * Reassert it until every guest context has actually exited. */
            g_core_preempt[0] = 1;
            uint64_t now = platform_now_ns();
            uint64_t left = now < deadline ? deadline - now : 0;
            rc = svcWaitSynchronizationSingle(w->thread.handle, left > 50000000 ? 50000000 : left);
            if (R_SUCCEEDED(rc) || R_VALUE(rc) != KERNELRESULT(TimedOut) || !left) break;
        }
        if (R_FAILED(rc)) {
            switch2_checkpoint("FAIL_WORKER_SHUTDOWN (do not return a live thread to hbloader)");
            rt_fatal("[switch2] worker%u did not terminate, rc0x%X; restart native title", i, rc);
        }
        rc = threadClose(&w->thread);
        if (R_FAILED(rc)) rt_fatal("[switch2] worker%u close failed 0x%X", i, rc);
        free(w); g_workers[i] = NULL;
    }
    g_nworkers = 0;
    switch2_checkpoint("WORKERS_JOINED_AND_CLOSED");
}

static void* g_ram[2];
static uint8_t* g_window;
static VirtmemReservation* g_reservation;
static const size_t k_sizes[] = {GUEST_RAM_SIZE, 0x10000};
static const size_t k_offsets[] = {0, LOCKED_CACHE_PHYS};
static unsigned g_mapped;
void mem_init(void) {
    switch2_checkpoint("BEGIN_GUEST_STACK_MAPPING");
    const size_t size = LOCKED_CACHE_PHYS + 0x10000ULL;
    virtmemLock();
    g_window = virtmemFindStack(size, 0x10000);
    g_reservation = g_window ? virtmemAddReservation(g_window, size) : NULL;
    virtmemUnlock();
    if (!g_reservation) rt_fatal("[switch2] cannot reserve guest VA window");
    for (unsigned i = 0; i < 2; i++) {
        g_ram[i] = aligned_alloc(4096, k_sizes[i]);
        if (!g_ram[i]) rt_fatal("[switch2] guest backing allocation failed");
        memset(g_ram[i], 0, k_sizes[i]);
        Result rc = svcMapMemory(g_window + k_offsets[i], g_ram[i], k_sizes[i]);
        if (R_FAILED(rc)) rt_fatal("[switch2] guest range%u mapping failed:0x%X", i, rc);
        g_mapped++;
    }
    ppc_mem_base_var = g_window;
    rt_log("[switch2] guest window=%p backed=%u+65536 bytes, RW stack SVC; no executable guest mapping", g_window, GUEST_RAM_SIZE);
    switch2_checkpoint("GUEST_MAPPING_COMPLETE");
}
void switch2_shutdown(void) {
    if (g_nworkers) switch2_stop_workers();
    switch2_audio_shutdown();
    for (unsigned i = g_mapped; i > 0; i--) {
        unsigned j = i - 1;
        Result rc = svcUnmapMemory(g_window + k_offsets[j], g_ram[j], k_sizes[j]);
        if (R_FAILED(rc)) rt_fatal("[switch2] guest unmap%u failed 0x%X", j, rc);
        free(g_ram[j]); g_ram[j] = NULL;
    }
    g_mapped = 0;
    if (g_reservation) { virtmemLock(); virtmemRemoveReservation(g_reservation); virtmemUnlock(); g_reservation = NULL; }
    appletUnhook(&g_hook);
    switch2_checkpoint("PRE_LIBC_SHUTDOWN_COMPLETE; awaiting filesystem EXIT_HOOK and tester hbmenu confirmation");
    switch2_log_stop();
}
void profile_register_thread(void) {}
/* Adapt the original Switch stall monitor, without enabling per-entry tracing
 * or exiting the app. Only dump after eight focused seconds without GX frames.
 * Nonblocking worker snapshot avoids acquiring a possibly stalled runtime lock. */
void sched_dump(void);
void mmio_dump(void);
unsigned long long gx_frames_presented(void);
static int stall_thread(void* arg) {
    (void)arg;
    rt_thread_pin(3);
    unsigned long long last = gx_frames_presented();
    unsigned still = 0;
    int reported = 0;
    unsigned samples = 0;
    for (;;) {
        platform_sleep_ns(1000000000ULL);
        unsigned long long frames = gx_frames_presented();
        if (++samples % 5 == 0) {
            uint64_t t = platform_now_ns();
            void sched_stream_dump(void);
            rt_log("[stream2] ns=%llu GX=%llu VI=%u fn=%08X lr=%08X", (unsigned long long)t, frames, vi_field_count(), g_ppc_cur_fn, g_ppc_cur_lr);
            sched_stream_dump();
            uint64_t log_write, log_flush, log_slow;
            void switch2_log_stats(uint64_t*, uint64_t*, uint64_t*);
            switch2_log_stats(&log_write, &log_flush, &log_slow);
            rt_log("[log2] ns=%llu max_write_ms=%.2f max_flush_ms=%.2f calls_over50ms=%llu (last interval)",
                   (unsigned long long)t, log_write/1e6, log_flush/1e6, (unsigned long long)log_slow);
            if (mutexTryLock(&g_workers_lock)) {
                for (unsigned i = 0; i < g_nworkers; i++) {
                    Worker* w = g_workers[i];
                    if (!w) continue;
                    unsigned kind = atomic_load(&w->wait_kind);
                    uint64_t since = atomic_load(&w->wait_since);
                    rt_log("[wait2] worker=%u arg=%p kind=%u object=%p age_ms=%llu max_ms=%llu waits=%u timeouts=%u",
                           i, w->arg, kind, (void*)atomic_load(&w->wait_object),
                           (unsigned long long)(kind && t >= since ? (t-since)/1000000 : 0),
                           (unsigned long long)(atomic_exchange(&w->wait_max_ns, 0)/1000000),
                           atomic_load(&w->wait_count), atomic_load(&w->wait_timeouts));
                }
                mutexUnlock(&g_workers_lock);
            }
            rt_log("[stream2] diagnostic_snapshot_ms=%.2f", (platform_now_ns()-t)/1e6);
        }
        if (frames != last || appletGetFocusState() != AppletFocusState_InFocus) {
            if (reported && frames != last) rt_log("[stall2] recovered after %u samples, frame %llu", still, frames);
            last = frames; still = 0; reported = 0;
            continue;
        }
        ++still;
        if (still < 8 || (reported && still != 16)) continue;
        reported = 1;
        rt_log("[stall2] no GX progress for %u samples frame=%llu retrace=%u guest_fn=%08X lr=%08X ns=%llu; observational snapshot",
               still, frames, vi_field_count(), g_ppc_cur_fn, g_ppc_cur_lr, (unsigned long long)platform_now_ns());
        switch2_log_flush();
        if (mutexTryLock(&g_workers_lock)) {
            for (unsigned i = 0; i < g_nworkers; i++) {
                Worker* w = g_workers[i];
                if (!w) continue;
                unsigned kind = atomic_load(&w->wait_kind);
                uint64_t since = atomic_load(&w->wait_since), now = platform_now_ns();
                rt_log("[stall2] worker%u fn=%p arg=%p exited=%d wait=%u object=%p age_ms=%llu waits=%u", i, (void*)w->fn, w->arg,
                       atomic_load(&w->exited), kind, (void*)atomic_load(&w->wait_object),
                       (unsigned long long)(kind && now >= since ? (now-since)/1000000 : 0), atomic_load(&w->wait_count));
            }
            mutexUnlock(&g_workers_lock);
        } else rt_log("[stall2] worker registry busy (not blocked for dump)");
        switch2_log_flush();
        /* Existing best-effort dump is intentionally unlocked, just as on
         * Switch1. Scheduler state may change during a snapshot. */
        mmio_dump();
        sched_dump();
        rt_log("[stall2] snapshot complete");
        switch2_log_flush();
    }
    return 0;
}
void profile_start(void) {
    switch2_log_start();
    rt_log("[stall2] experimental no-progress diagnostics: 8s/16s samples; no guest trace or forced exit");
    rt_thread_start(stall_thread, NULL);
}
void profile_dump(void) {}
void platform_sample_threads(void) {}
