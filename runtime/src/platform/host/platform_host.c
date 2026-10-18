/* Desktop platform layer: paths, threads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
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
    SetUnhandledExceptionFilter(crash_handler);
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
