/* C11 <threads.h> for Apple SDKs, which do not ship it (iOS and macOS). Only the part the runtime uses:
 * mutexes, condition variables, call_once and thrd_sleep, on pthreads. configure.py puts this folder on the
 * include path for Apple targets only. */
#ifndef SR_APPLE_THREADS_H
#define SR_APPLE_THREADS_H

#include <errno.h>
#include <pthread.h>
#include <time.h>

enum { thrd_success = 0, thrd_nomem, thrd_timedout, thrd_busy, thrd_error };
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
static inline void mtx_destroy(mtx_t* m) { pthread_mutex_destroy(m); }
static inline int mtx_lock(mtx_t* m) { return pthread_mutex_lock(m) ? thrd_error : thrd_success; }
static inline int mtx_trylock(mtx_t* m) {
    int r = pthread_mutex_trylock(m);
    return r == EBUSY ? thrd_busy : r ? thrd_error : thrd_success;
}
static inline int mtx_unlock(mtx_t* m) { return pthread_mutex_unlock(m) ? thrd_error : thrd_success; }

static inline int cnd_init(cnd_t* c) { return pthread_cond_init(c, NULL) ? thrd_error : thrd_success; }
static inline void cnd_destroy(cnd_t* c) { pthread_cond_destroy(c); }
static inline int cnd_signal(cnd_t* c) { return pthread_cond_signal(c) ? thrd_error : thrd_success; }
static inline int cnd_broadcast(cnd_t* c) { return pthread_cond_broadcast(c) ? thrd_error : thrd_success; }
static inline int cnd_wait(cnd_t* c, mtx_t* m) { return pthread_cond_wait(c, m) ? thrd_error : thrd_success; }
/* ts: an absolute TIME_UTC (CLOCK_REALTIME) time, as in C11 and as pthread_cond_timedwait expects */
static inline int cnd_timedwait(cnd_t* c, mtx_t* m, const struct timespec* ts) {
    int r = pthread_cond_timedwait(c, m, ts);
    return r == ETIMEDOUT ? thrd_timedout : r ? thrd_error : thrd_success;
}

static inline void call_once(once_flag* f, void (*fn)(void)) { pthread_once(f, fn); }

static inline int thrd_sleep(const struct timespec* d, struct timespec* rem) {
    return nanosleep(d, rem) ? -1 : 0;
}

#endif
