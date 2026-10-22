/* C11 <threads.h> for toolchains whose C library lacks it (llvm-mingw: the setup tool's PC build).
 * Only what the runtime uses: mutexes (plain, recursive), condition variables, call_once and
 * thrd_sleep, on Win32 critical sections, condition variables and init-once. configure.py adds
 * runtime/include/compat to the include path for that toolchain only. */
#pragma once
#ifndef _WIN32
#error "compat/threads.h is the Win32 fallback; use the C library's <threads.h> elsewhere"
#endif
#include <time.h>
#include <windows.h>

enum { thrd_success = 0, thrd_nomem = 1, thrd_timedout = 2, thrd_busy = 3, thrd_error = 4 };
enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };

typedef CRITICAL_SECTION mtx_t;  /* recursive by nature, which also serves mtx_plain */
typedef CONDITION_VARIABLE cnd_t;
typedef INIT_ONCE once_flag;
#define ONCE_FLAG_INIT INIT_ONCE_STATIC_INIT

static inline int mtx_init(mtx_t* m, int type) { (void)type; InitializeCriticalSection(m); return thrd_success; }
static inline int mtx_lock(mtx_t* m) { EnterCriticalSection(m); return thrd_success; }
static inline int mtx_trylock(mtx_t* m) { return TryEnterCriticalSection(m) ? thrd_success : thrd_busy; }
static inline int mtx_unlock(mtx_t* m) { LeaveCriticalSection(m); return thrd_success; }
static inline void mtx_destroy(mtx_t* m) { DeleteCriticalSection(m); }

static inline int cnd_init(cnd_t* c) { InitializeConditionVariable(c); return thrd_success; }
static inline int cnd_signal(cnd_t* c) { WakeConditionVariable(c); return thrd_success; }
static inline int cnd_broadcast(cnd_t* c) { WakeAllConditionVariable(c); return thrd_success; }
static inline void cnd_destroy(cnd_t* c) { (void)c; }
static inline int cnd_wait(cnd_t* c, mtx_t* m) {
    return SleepConditionVariableCS(c, m, INFINITE) ? thrd_success : thrd_error;
}
/* `until` is an absolute TIME_UTC time, as in C11 */
static inline int cnd_timedwait(cnd_t* c, mtx_t* m, const struct timespec* until) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    long long ns = (long long)(until->tv_sec - now.tv_sec) * 1000000000 + (until->tv_nsec - now.tv_nsec);
    if (ns <= 0) return thrd_timedout;
    long long ms = (ns + 999999) / 1000000;  /* round up: a 0 ms wait would make the caller spin */
    if (SleepConditionVariableCS(c, m, (DWORD)ms)) return thrd_success;
    return GetLastError() == ERROR_TIMEOUT ? thrd_timedout : thrd_error;
}

static BOOL CALLBACK compat_once_cb(PINIT_ONCE once, PVOID fn, PVOID* ctx) {
    (void)once; (void)ctx;
    ((void (*)(void))fn)();
    return TRUE;
}
static inline void call_once(once_flag* flag, void (*fn)(void)) {
    InitOnceExecuteOnce(flag, compat_once_cb, (PVOID)fn, NULL);
}

static inline int thrd_sleep(const struct timespec* d, struct timespec* rem) {
    (void)rem;
    Sleep((DWORD)(d->tv_sec * 1000 + (d->tv_nsec + 999999) / 1000000));
    return 0;
}
