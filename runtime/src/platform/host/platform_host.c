/* Desktop platform layer: paths, threads. */
#if defined(__ANDROID__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE  /* cpu_set_t, sched_setaffinity (rt_thread_pin) */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime.h"
#include <SDL.h>
#ifndef _WIN32
#include <unistd.h>
#endif

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
/* laptops with two graphics chips (NVIDIA Optimus, AMD switchable graphics): the drivers look for these in the
 * exe and then run the game on the dedicated chip instead of the integrated one */
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
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

#ifndef _WIN32
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#ifdef __APPLE__
#include <sys/ucontext.h>  /* <ucontext.h> needs _XOPEN_SOURCE on Apple; only the type is used */
#else
#include <ucontext.h>
#endif
#include <unistd.h>
#include <unwind.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif
void cpu_dump_trace(void);
void mmio_dump(void);
void sched_dump(void);
extern const char g_build_version[];

/* a native crash (Android, Linux), as the Windows crash_handler: the report goes to crash.txt first
 * (plain write(), works even if the log's stdio lock is held), then to logcat and the log, then the
 * guest state; then the previous handler (Android's debuggerd: tombstone) runs. On Android the app
 * folder's crash.txt and sonicriders.log are picked up on the next launch (CrashReport.java). */
static struct sigaction crash_old[NSIG];
static char crash_buf[16384];
static int crash_len;

static void crash_add(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int room = (int)sizeof crash_buf - crash_len;
    int n = room > 1 ? vsnprintf(crash_buf + crash_len, (size_t)room, fmt, ap) : 0;
    va_end(ap);
    if (n > 0) crash_len += n < room ? n : room - 1;
}

static void crash_frame(int i, uintptr_t pc) {
    Dl_info di;
    if (pc && dladdr((void*)pc, &di) && di.dli_fname) {
        const char* lib = strrchr(di.dli_fname, '/');
        crash_add("[crash] #%02d pc %p %s+%llX%s%s\n", i, (void*)pc, lib ? lib + 1 : di.dli_fname,
                  (unsigned long long)(pc - (uintptr_t)di.dli_fbase), di.dli_sname ? " " : "",
                  di.dli_sname ? di.dli_sname : "");
    } else {
        crash_add("[crash] #%02d pc %p\n", i, (void*)pc);
    }
}

struct crash_bt { int n; };
static _Unwind_Reason_Code crash_bt_step(struct _Unwind_Context* c, void* arg) {
    struct crash_bt* b = arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(c);
    if (!pc) return _URC_END_OF_STACK;
    crash_frame(b->n, pc);
    return ++b->n >= 40 ? _URC_END_OF_STACK : _URC_NO_REASON;
}

static void crash_signal(int sig, siginfo_t* si, void* uctx) {
    static volatile sig_atomic_t once;
    sigaction(sig, &crash_old[sig], NULL);  /* returning re-runs the fault (or abort) into the old handler */
    if (once) return;
    once = 1;
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL"
                     : sig == SIGFPE ? "SIGFPE" : sig == SIGABRT ? "SIGABRT" : "signal";
    crash_add("[crash] %s (%d) code %d, build %s\n", name, sig, si->si_code, g_build_version);
    if (si->si_code > 0 && sig != SIGABRT)  /* a fault (not kill/abort): the address it touched */
        crash_add("[crash] address %p (guest window offset %llX)\n", si->si_addr,
                  (unsigned long long)((uintptr_t)si->si_addr - (uintptr_t)ppc_mem_base_var));
    ucontext_t* uc = uctx;
    (void)uc;
#if defined(__APPLE__) && defined(__aarch64__)
    crash_add("[crash] at:\n");
    crash_frame(0, (uintptr_t)uc->uc_mcontext->__ss.__pc);
    crash_add("[crash] lr:\n");
    crash_frame(0, (uintptr_t)uc->uc_mcontext->__ss.__lr);
#elif defined(__aarch64__)
    crash_add("[crash] at:\n");
    crash_frame(0, (uintptr_t)uc->uc_mcontext.pc);
    crash_add("[crash] lr:\n");
    crash_frame(0, (uintptr_t)uc->uc_mcontext.regs[30]);
#elif defined(__x86_64__) && defined(REG_RIP)
    crash_add("[crash] at:\n");
    crash_frame(0, (uintptr_t)uc->uc_mcontext.gregs[REG_RIP]);
#endif
    crash_add("[crash] stack:\n");
    struct crash_bt bt = {0};
    _Unwind_Backtrace(crash_bt_step, &bt);
    int fd = open("crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, crash_buf, (size_t)crash_len);
        close(fd);
    }
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_FATAL, "sonicriders", crash_buf);
#endif
    fflush(stderr);
    write(fileno(stderr), crash_buf, (size_t)crash_len);
    if (g_ppc_trace) cpu_dump_trace();
    mmio_dump();
    sched_dump();
    fflush(stderr);
    if (si->si_code <= 0) raise(sig);  /* sent (kill, abort): a fault would not come back on return */
}

static void crash_install(void) {
    /* a stack overflow needs its own stack for the handler (this thread only) */
    static char alt[64 * 1024];
    stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt};
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, &crash_old[sigs[i]]);
}
#endif

#if defined(__ANDROID__) || defined(SR_IOS)
#include <SDL_system.h>
#include <unistd.h>
/* Android: everything lives in the app's own folder, /sdcard/Android/data/<package>/files (adb push/pull
 * reach it; uninstalling the app deletes it): baserom/<image>.iso, saves/, mods/, env.txt and the log.
 * env.txt holds KEY=VALUE lines (as on the Switch); launch arguments (SonicRidersActivity "args") win.
 * iOS: the same layout in the app's Documents folder (Files app "On My iPhone", Finder, devicectl copy;
 * deleting the app deletes it); environment from `devicectl ... launch --environment-variables` wins. */
static void android_files(void) {
#ifdef SR_IOS
    static char docs[1024];
    const char* home = getenv("HOME");
    snprintf(docs, sizeof docs, "%s/Documents", home ? home : ".");
    const char* dir = docs;
    if (chdir(dir) != 0) dir = NULL;
#else
    const char* dir = SDL_AndroidGetExternalStoragePath();
    if (!dir || chdir(dir) != 0) dir = SDL_AndroidGetInternalStoragePath();
    if (dir && chdir(dir) != 0) dir = NULL;
#endif
    remove("sonicriders.prev.log");
    rename("sonicriders.log", "sonicriders.prev.log");
    freopen("sonicriders.log", "w", stderr);
    mkdir("baserom", 0777);
    FILE* f = fopen("env.txt", "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            char* eq = strchr(line, '=');
            if (line[0] == '#' || !eq) continue;
            *eq = 0;
            char* v = eq + 1;
            v[strcspn(v, "\r\n")] = 0;
            setenv(line, v, 0);
        }
        fclose(f);
    }
}

/* KEY=VALUE launch words become environment variables; other words stay arguments (argv[1]: a disc path) */
int platform_android_args(int argc, char** argv) {
    int n = 1;
    for (int i = 1; i < argc; i++) {
        char* eq = strchr(argv[i], '=');
        if (eq && eq != argv[i]) {
            *eq = 0;
            setenv(argv[i], eq + 1, 1);
            *eq = '=';
        } else {
            argv[n++] = argv[i];
        }
    }
    return n;
}
#endif

void platform_init(void) {
#if defined(__ANDROID__) || defined(SR_IOS)
    android_files();
#endif
#ifdef _WIN32
    /* started as a normal windowed program (no console, stderr not redirected): the log goes to
     * sonicriders.log next to the game; the four runs before it are kept as sonicriders.prev.log (the last one,
     * as on the Switch and iOS), .prev2.log, .prev3.log and .prev4.log (the oldest) */
    HANDLE eh = GetStdHandle(STD_ERROR_HANDLE);
    if (eh == NULL || eh == INVALID_HANDLE_VALUE || GetFileType(eh) == FILE_TYPE_UNKNOWN) {
        static const char* const keep[] = {"sonicriders.log", "sonicriders.prev.log", "sonicriders.prev2.log",
                                           "sonicriders.prev3.log", "sonicriders.prev4.log"};
        for (int i = 4; i > 0; i--) {
            remove(keep[i]);
            rename(keep[i - 1], keep[i]);
        }
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
#ifndef _WIN32
    crash_install();
#endif
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
    /* iOS: also the top of Documents, where Finder's Files tab (Sideloadly installs) drops the image; it
     * cannot make the baserom folder there */
#ifdef SR_IOS
    static const char* dirs[] = {"baserom", "."};
#else
    static const char* dirs[] = {"baserom"};
#endif
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        DIR* d = opendir(dirs[i]);
        if (!d) continue;
        struct dirent* e;
        int found = 0;
        while ((e = readdir(d)))
            if (strlen(e->d_name) > 4 && !strcmp(e->d_name + strlen(e->d_name) - 4, ".iso")) {
                snprintf(path, sizeof path, "%s/%s", dirs[i], e->d_name);
                found = 1;
                break;
            }
        closedir(d);
        if (found) break;
    }
#endif
    return path;
}

/* SR_CARD=<file>: another memory card image (determinism tests run every machine on the same save) */
const char* platform_save_path(void) { return getenv("SR_CARD") ? getenv("SR_CARD") : "saves/card_a.raw"; }
const char* platform_settings_path(void) { return "saves/settings.ini"; }
const char* platform_mods_dir(void) { return "mods"; }  /* in the install folder */

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

double platform_gpu_frame_ms(void) { return -1.0; }  /* not measured by the GL renderer */
#ifdef __ANDROID__
/* Android (#14: 50-150 ms game-thread stalls on an Adreno 810 phone, even on the title): the scheduler may park
 * a thread on a little core or behind other work. The threads the runtime pins (game 0, render 1, GP / devices 2,
 * watchdog 3) go to the fast cores (every core above the slowest max-frequency tier) and get a higher priority
 * than normal app threads: game -10, audio mixer -16 (raise), render / GP helpers -4 (lower). Android lets apps
 * lower nice to -20 (RLIMIT_NICE). SR_NOPIN=1 turns it off (A/B). */
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
static cpu_set_t g_fast_cpus;
static int g_fast_n;  /* 0: off or unknown */
static void find_fast_cpus(void) {
    if (getenv("SR_NOPIN")) {
        rt_log("[android] thread placement off (SR_NOPIN)");
        return;
    }
    long f[64], lo = 0, hi = 0;
    int n = 0;
    for (; n < 64; n++) {
        char p[96];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", n);
        FILE* fp = fopen(p, "r");
        if (!fp) break;
        if (fscanf(fp, "%ld", &f[n]) != 1) f[n] = 0;
        fclose(fp);
        if (f[n] && (!lo || f[n] < lo)) lo = f[n];
        if (f[n] > hi) hi = f[n];
    }
    if (!n || !hi) {
        rt_log("[android] core frequencies unknown: threads not pinned");
        return;
    }
    char list[256] = "";
    CPU_ZERO(&g_fast_cpus);
    for (int i = 0; i < n; i++)
        if (f[i] > lo || hi == lo) {
            CPU_SET(i, &g_fast_cpus);
            g_fast_n++;
            snprintf(list + strlen(list), sizeof list - strlen(list), " %d (%ld MHz)", i, f[i] / 1000);
        }
    rt_log("[android] %d cores, fast ones:%s; game threads go there", n, list);
}
static void set_nice(int nice) {
    if (g_fast_n && setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice))
        rt_log("[android] setpriority(%d) failed", nice);
}
void rt_thread_pin(int core) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, find_fast_cpus);
    if (!g_fast_n) return;
    if (sched_setaffinity(0, sizeof g_fast_cpus, &g_fast_cpus)) rt_log("[android] sched_setaffinity failed");
    set_nice(core == 0 ? -10 : core == 3 ? 0 : -4);
}
void rt_thread_lower_priority(void) { set_nice(-4); }
void rt_thread_raise_priority(void) { set_nice(-16); }

/* ADPF performance hint session (#14): the game thread reports its work per game step, so the system can raise
 * the CPU clock before a step misses its frame instead of after. Android 13+ (API 33) only: looked up at run
 * time (the app supports Android 11). SR_NOHINT=1 turns it off (A/B). */
#include <dlfcn.h>
typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
void platform_game_step_work(uint64_t work_ns) {
    static int state;  /* 0 not tried, 1 on, -1 off */
    static APerformanceHintSession* s;
    static int (*report)(APerformanceHintSession*, int64_t);
    if (state < 0) return;
    if (!state) {
        state = -1;
        if (getenv("SR_NOHINT")) {
            rt_log("[android] performance hint off (SR_NOHINT)");
            return;
        }
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        APerformanceHintManager* (*get)(void) = lib ? (APerformanceHintManager * (*)(void)) dlsym(lib, "APerformanceHint_getManager") : NULL;
        APerformanceHintSession* (*create)(APerformanceHintManager*, const int32_t*, size_t, int64_t) =
            lib ? (APerformanceHintSession * (*)(APerformanceHintManager*, const int32_t*, size_t, int64_t))
                      dlsym(lib, "APerformanceHint_createSession")
                : NULL;
        report = lib ? (int (*)(APerformanceHintSession*, int64_t))dlsym(lib, "APerformanceHint_reportActualWorkDuration") : NULL;
        APerformanceHintManager* m = get ? get() : NULL;
        int32_t tid = (int32_t)syscall(SYS_gettid);
        s = m && create && report ? create(m, &tid, 1, 16666667) : NULL;
        if (!s) {
            rt_log("[android] performance hint not available (Android 13+ and device support needed)");
            return;
        }
        state = 1;
        rt_log("[android] performance hint session for the game thread (target 16.7 ms per step)");
    }
    if (work_ns > 0 && work_ns < 2000000000ull) report(s, (int64_t)work_ns);
}
#elif defined(SR_IOS)
/* iOS: no affinity or nice for apps; quality-of-service classes instead. The pinned runtime threads (game 0,
 * render 1, GP / devices 2) are user-interactive, the watchdog (3) utility; the audio mixer stays
 * user-interactive (SDL's CoreAudio thread is real-time already). */
#include <pthread/qos.h>
void rt_thread_pin(int core) {
    pthread_set_qos_class_self_np(core == 3 ? QOS_CLASS_UTILITY : QOS_CLASS_USER_INTERACTIVE, 0);
}
void rt_thread_lower_priority(void) { pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0); }
void rt_thread_raise_priority(void) { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }
#else
void rt_thread_pin(int core) { (void)core; }
void rt_thread_lower_priority(void) {}
void rt_thread_raise_priority(void) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
}
#endif
void platform_set_handheld_gpu(int on) { (void)on; }

/* ---- "Save a bug report" (settings menu, About): one zip in the game folder with what a report needs:
 * sonicriders.log (this run, flushed first), sonicriders.prev.log (.prev2-.prev4 on PC), setup.log, saves/settings.ini, crash.txt
 * and info.txt (version, system). Stored (uncompressed) zip: no library needed. Not on Android (its own
 * crash reports, CrashReport.java). Returns 0 and the file's name, or -1. */
#ifndef __ANDROID__
extern const char g_build_version[];
static uint32_t zip_crc(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}
static void put16(FILE* f, unsigned v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }
static void put32(FILE* f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }
static uint8_t* slurp(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > 64L * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t* b = malloc((size_t)len + 1);
    *n = b ? fread(b, 1, (size_t)len, f) : 0;
    fclose(f);
    return b;
}
static void system_info(char* out, size_t n) {
    size_t k = (size_t)snprintf(out, n, "Sonic Riders native port bug report\r\nversion: %s\r\n", g_build_version);
    time_t t = time(NULL);
    k += strftime(out + k, n - k, "saved: %Y-%m-%d %H:%M:%S\r\n", localtime(&t));
#ifdef _WIN32
    OSVERSIONINFOA vi = {sizeof vi};
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOA*);
    RtlGetVersionFn rgv = (RtlGetVersionFn)(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (rgv) rgv(&vi);
    k += (size_t)snprintf(out + k, n - k, "system: Windows %lu.%lu build %lu\r\n", vi.dwMajorVersion,
                          vi.dwMinorVersion, vi.dwBuildNumber);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    MEMORYSTATUSEX ms = {sizeof ms};
    GlobalMemoryStatusEx(&ms);
    k += (size_t)snprintf(out + k, n - k, "cpu threads: %lu, memory: %llu MB\r\n", si.dwNumberOfProcessors,
                          (unsigned long long)(ms.ullTotalPhys >> 20));
#else
    FILE* f = fopen("/etc/os-release", "r");
    char line[256];
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "PRETTY_NAME=", 12)) k += (size_t)snprintf(out + k, n - k, "system: %s", line + 12);
    if (f) fclose(f);
    f = fopen("/proc/cpuinfo", "r");
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "model name", 10)) { k += (size_t)snprintf(out + k, n - k, "cpu: %s", strchr(line, ':') + 2); break; }
    if (f) fclose(f);
    const char* envs[] = {"SteamDeck", "GAMESCOPE_WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE",
                          "WAYLAND_DISPLAY", "DISPLAY", "SDL_VIDEODRIVER"};
    for (size_t i = 0; i < sizeof envs / sizeof envs[0]; i++)
        k += (size_t)snprintf(out + k, n - k, "%s=%s\n", envs[i], getenv(envs[i]) ? getenv(envs[i]) : "");
#endif
    (void)k;
}
int platform_bug_report(char* name, size_t n) {
    fflush(stderr);
    time_t t = time(NULL);
    strftime(name, n, "bug-report-%Y%m%d-%H%M%S.zip", localtime(&t));
    FILE* z = fopen(name, "wb");
    if (!z) return -1;
    static const char* files[] = {"info.txt", "sonicriders.log", "sonicriders.prev.log", "sonicriders.prev2.log",
                                  "sonicriders.prev3.log", "sonicriders.prev4.log", "setup.log",
                                  "saves/settings.ini", "crash.txt"};
    struct { const char* name; uint32_t crc, size, off; } ent[sizeof files / sizeof files[0]];
    int ne = 0;
    char info[4096];
    system_info(info, sizeof info);
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        size_t len = 0;
        uint8_t* data = i == 0 ? (uint8_t*)info : slurp(files[i], &len);
        if (i == 0) len = strlen(info);
        if (!data) continue;
        uint32_t crc = zip_crc(data, len), off = (uint32_t)ftell(z);
        size_t nl = strlen(files[i]);
        put32(z, 0x04034b50); put16(z, 10); put16(z, 0); put16(z, 0); put16(z, 0); put16(z, 0x21);
        put32(z, crc); put32(z, (uint32_t)len); put32(z, (uint32_t)len); put16(z, (unsigned)nl); put16(z, 0);
        fwrite(files[i], 1, nl, z);
        fwrite(data, 1, len, z);
        if (i) free(data);
        ent[ne].name = files[i], ent[ne].crc = crc, ent[ne].size = (uint32_t)len, ent[ne].off = off, ne++;
    }
    uint32_t cd = (uint32_t)ftell(z);
    for (int i = 0; i < ne; i++) {
        size_t nl = strlen(ent[i].name);
        put32(z, 0x02014b50); put16(z, 20); put16(z, 10); put16(z, 0); put16(z, 0); put16(z, 0); put16(z, 0x21);
        put32(z, ent[i].crc); put32(z, ent[i].size); put32(z, ent[i].size); put16(z, (unsigned)nl);
        put16(z, 0); put16(z, 0); put16(z, 0); put16(z, 0); put32(z, 0); put32(z, ent[i].off);
        fwrite(ent[i].name, 1, nl, z);
    }
    uint32_t cdlen = (uint32_t)ftell(z) - cd;
    put32(z, 0x06054b50); put16(z, 0); put16(z, 0); put16(z, (unsigned)ne); put16(z, (unsigned)ne);
    put32(z, cdlen); put32(z, cd); put16(z, 0);
    int bad = ferror(z);
    fclose(z);
    if (bad) return -1;
    rt_log("[report] saved %s (%d files)", name, ne);
    return 0;
}
/* show the game folder (the bug report is there) */
void platform_open_game_folder(void) {
    char dir[1024], url[1100];
#ifdef _WIN32
    if (!GetCurrentDirectoryA(sizeof dir, dir)) return;
    for (char* c = dir; *c; c++)
        if (*c == '\\') *c = '/';
    snprintf(url, sizeof url, "file:///%s", dir);
#else
    if (!getcwd(dir, sizeof dir)) return;
    snprintf(url, sizeof url, "file://%s", dir);
#endif
    SDL_OpenURL(url);
}
#endif
