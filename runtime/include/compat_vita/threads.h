/* C11 <threads.h> for the PS Vita: VitaSDK's newlib ships the header without its machine part. Only what
 * the runtime uses (mutexes plain/recursive, condition variables, call_once, thrd_sleep), on pthreads.
 * configure.py puts runtime/include/compat_vita first on the include path for the vita target only. */
#pragma once
#ifndef __vita__
#error "compat_vita/threads.h is for the PS Vita build"
#endif
#include <errno.h>
#include <pthread.h>
#include <time.h>

enum { thrd_success = 0, thrd_nomem = 1, thrd_timedout = 2, thrd_busy = 3, thrd_error = 4 };
enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };

typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_once_t once_flag;
#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT

static inline int mtx_init(mtx_t* m, int type) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    if (type & mtx_recursive) pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    int r = pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return r ? thrd_error : thrd_success;
}
static inline int mtx_lock(mtx_t* m) { return pthread_mutex_lock(m) ? thrd_error : thrd_success; }
static inline int mtx_unlock(mtx_t* m) { return pthread_mutex_unlock(m) ? thrd_error : thrd_success; }
static inline int mtx_trylock(mtx_t* m) { return pthread_mutex_trylock(m) ? thrd_busy : thrd_success; }
static inline void mtx_destroy(mtx_t* m) { pthread_mutex_destroy(m); }

static inline int cnd_init(cnd_t* c) { return pthread_cond_init(c, NULL) ? thrd_error : thrd_success; }
/* A bounded wait: on the Vita a wake-up was lost (2026-11-04, probe with SR_PROFILE: the null renderer stayed
 * in cnd_wait after a broadcast, the GP thread waited for it, the game hung at the same scene in 3 of 3
 * runs). C11 allows spurious wake-ups and every caller re-checks its condition in a loop, so waking every
 * 5 ms turns a lost wake-up into a short delay instead of a hang. */
static inline int cnd_wait(cnd_t* c, mtx_t* m) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 5000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    int r = pthread_cond_timedwait(c, m, &ts);
    return r == 0 || r == ETIMEDOUT ? thrd_success : thrd_error;
}
static inline int cnd_timedwait(cnd_t* c, mtx_t* m, const struct timespec* abs) {
    int r = pthread_cond_timedwait(c, m, abs);  /* CLOCK_REALTIME deadline, as timespec_get(TIME_UTC) */
    return r == ETIMEDOUT ? thrd_timedout : r ? thrd_error : thrd_success;
}
static inline int cnd_signal(cnd_t* c) { return pthread_cond_signal(c) ? thrd_error : thrd_success; }
static inline int cnd_broadcast(cnd_t* c) { return pthread_cond_broadcast(c) ? thrd_error : thrd_success; }
static inline void cnd_destroy(cnd_t* c) { pthread_cond_destroy(c); }

static inline void call_once(once_flag* f, void (*fn)(void)) { pthread_once(f, fn); }

static inline int thrd_sleep(const struct timespec* d, struct timespec* rem) {
    (void)rem;
    return nanosleep(d, NULL) ? -1 : 0;
}
