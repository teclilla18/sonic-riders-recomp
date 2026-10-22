/* Desktop platform layer: paths, threads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#endif

#ifdef _WIN32
void cpu_dump_trace(void);
void mmio_dump(void);
void sched_dump(void);
extern int g_ppc_trace;

/* a host crash (usually a bad guest pointer reaching host code): log where, then the guest's recent
 * function entries (with SR_WATCHDOG) and its threads */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    HMODULE mod = GetModuleHandleA(NULL);
    rt_log("[crash] exception %08lX at %p (exe+%llX)", r->ExceptionCode, r->ExceptionAddress,
           (unsigned long long)((uintptr_t)r->ExceptionAddress - (uintptr_t)mod));
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        rt_log("[crash] %s address %p (guest window offset %llX)", r->ExceptionInformation[0] ? "writing" : "reading",
               (void*)r->ExceptionInformation[1],
               (unsigned long long)((uintptr_t)r->ExceptionInformation[1] - (uintptr_t)ppc_mem_base_var));
    if (g_ppc_trace) cpu_dump_trace();
    mmio_dump();
    sched_dump();
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

#ifdef _WIN32
#include <tlhelp32.h>
/* debugging (SR_WATCH): freeze every other thread of the process and log where each one is, as
 * exe+offset (map with build/host/sonicriders.map: recompiled functions are f_<guest address>) */
void platform_sample_threads(void) {
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {.dwSize = sizeof te};
    HANDLE held[256];
    uint64_t rip[256];
    DWORD ids[256];
    int n = 0;
    for (BOOL ok = Thread32First(snap, &te); ok && n < 256; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
        if (!h) continue;
        SuspendThread(h);
        held[n] = h;
        ids[n] = te.th32ThreadID;
        n++;
    }
    CloseHandle(snap);
    for (int i = 0; i < n; i++) {
        CONTEXT ctx = {.ContextFlags = CONTEXT_CONTROL};
        rip[i] = GetThreadContext(held[i], &ctx) ? ctx.Rip : 0;
    }
    for (int i = 0; i < n; i++) {
        ResumeThread(held[i]);
        CloseHandle(held[i]);
    }
    for (int i = 0; i < n; i++)
        if (rip[i] >= base && rip[i] < base + 0x10000000)
            rt_log("[sample] thread %lu at exe+%llX", ids[i], (unsigned long long)(rip[i] - base));
}
#else
void platform_sample_threads(void) {}
#endif

void platform_init(void) {
#ifdef _WIN32
    /* started as a normal windowed program (no console, stderr not redirected): the log goes to
     * sonicriders.log next to the game, the previous one kept as sonicriders.prev.log (as on the Switch) */
    HANDLE eh = GetStdHandle(STD_ERROR_HANDLE);
    if (eh == NULL || eh == INVALID_HANDLE_VALUE || GetFileType(eh) == FILE_TYPE_UNKNOWN) {
        remove("sonicriders.prev.log");
        rename("sonicriders.log", "sonicriders.prev.log");
        freopen("sonicriders.log", "w", stderr);
    }
#endif
#ifdef _WIN32
    /* the memory card and settings.ini live in saves\ (a fresh install has none yet: without it
     * saving failed silently) */
    CreateDirectoryA("saves", NULL);
#else
    mkdir("saves", 0777);
#endif
    /* stderr is the log: buffer it (rt_log flushes about once a second, or every line with
     * SR_LOGSYNC); an unbuffered stderr wrote each piece of a line separately */
    static char logbuf[64 * 1024];
    setvbuf(stderr, logbuf, _IOFBF, sizeof logbuf);
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
    /* 1 ms timer resolution: the device threads sleep for fractions of a millisecond (the AI clock
     * paces 5 ms audio frames); at the default 15.6 ms tick the audio ran at a third of its speed */
    timeBeginPeriod(1);
    /* Windows 11 ignores that request while the process has no visible window (minimized test runs)
     * unless the process opts out of timer-resolution throttling */
    PROCESS_POWER_THROTTLING_STATE pt = {0};
    pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pt.ControlMask = 0x4;  /* PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION */
    pt.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);
#endif
}

/* the first *.iso in baserom/ (the working copy of the user's disc) */
const char* platform_disc_path(void) {
    static char path[512] = "baserom/game.iso";
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("baserom\\*.iso", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        snprintf(path, sizeof path, "baserom/%s", fd.cFileName);
        FindClose(h);
    }
#else
    DIR* d = opendir("baserom");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)))
            if (strlen(e->d_name) > 4 && !strcmp(e->d_name + strlen(e->d_name) - 4, ".iso")) {
                snprintf(path, sizeof path, "baserom/%s", e->d_name);
                break;
            }
        closedir(d);
    }
#endif
    return path;
}

const char* platform_save_path(void) { return "saves/card_a.raw"; }
const char* platform_settings_path(void) { return "saves/settings.ini"; }

/* a precise sleep: Windows' high-resolution waitable timer (Sleep and the C11 waits round up to the
 * timer tick); elsewhere nanosleep */
void platform_sleep_ns(uint64_t ns) {
#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    static __thread HANDLE timer;
    if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ns / 100);  /* relative, 100 ns units */
        if (due.QuadPart == 0) due.QuadPart = -1;
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    Sleep((DWORD)(ns / 1000000));
#else
    struct timespec ts = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
    nanosleep(&ts, NULL);
#endif
}

uint64_t platform_now_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

void platform_local_time(int* year, int* month, int* day, int* hour, int* minute, int* second) {
    time_t t = time(NULL);
    struct tm lt = *localtime(&t);
    *year = lt.tm_year + 1900; *month = lt.tm_mon + 1; *day = lt.tm_mday;
    *hour = lt.tm_hour; *minute = lt.tm_min; *second = lt.tm_sec;
}

typedef struct { int (*fn)(void*); void* arg; } Start;

#ifdef _WIN32
static DWORD WINAPI trampoline(LPVOID p) {
    Start s = *(Start*)p;
    free(p);
    return (DWORD)s.fn(s.arg);
}
void rt_thread_start(int (*fn)(void*), void* arg) {
    Start* s = malloc(sizeof *s);
    s->fn = fn;
    s->arg = arg;
    HANDLE h = CreateThread(NULL, 8 << 20, trampoline, s, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!h) rt_fatal("cannot create a thread");
    CloseHandle(h);
}
#else
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
    pthread_attr_setstacksize(&a, 8 << 20);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    if (pthread_create(&t, &a, trampoline, s)) rt_fatal("cannot create a thread");
    pthread_attr_destroy(&a);
}
#endif

void rt_thread_pin(int core) { (void)core; }
void rt_thread_lower_priority(void) {}
