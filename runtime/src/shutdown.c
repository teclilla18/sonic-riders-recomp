/* Android cannot run process-wide C++ finalizers while framework/vendor RenderThread workers
 * still use those libraries. Run OUR persistent-data handlers after explicit renderer/SDL cleanup,
 * flush streams, then terminate the process without destroying other threads' library globals.
 * Other platforms keep the existing libc atexit/exit behaviour. */
#include <stdio.h>
#include <stdlib.h>
#include "runtime.h"

#ifdef __ANDROID__
#include <stdatomic.h>
#include <threads.h>

enum { EXIT_HANDLERS = 32 };
static void (*g_exit_handlers[EXIT_HANDLERS])(void);
static unsigned g_exit_count;
static int g_exit_closed;
static once_flag g_exit_once = ONCE_FLAG_INIT;
static mtx_t g_exit_lock;
static atomic_flag g_exit_draining = ATOMIC_FLAG_INIT;
static int g_exit_registration;

static void run_exit_handlers(void);
static void exit_init(void) {
    mtx_init(&g_exit_lock, mtx_plain);
    /* Retain handlers for any unrelated normal-exit path, but execute each only once. */
    g_exit_registration = atexit(run_exit_handlers);
}

int rt_atexit(void (*handler)(void)) {
    call_once(&g_exit_once, exit_init);
    mtx_lock(&g_exit_lock);
    int failed = g_exit_registration || g_exit_closed || g_exit_count == EXIT_HANDLERS || !handler;
    if (!failed) g_exit_handlers[g_exit_count++] = handler;
    mtx_unlock(&g_exit_lock);
    return failed;
}

static void run_exit_handlers(void) {
    call_once(&g_exit_once, exit_init);
    if (atomic_flag_test_and_set(&g_exit_draining)) return;
    for (;;) {
        mtx_lock(&g_exit_lock);
        if (!g_exit_count) {
            g_exit_closed = 1;
            mtx_unlock(&g_exit_lock);
            return;
        }
        void (*handler)(void) = g_exit_handlers[--g_exit_count];
        mtx_unlock(&g_exit_lock);
        handler();  /* allow a handler to register another one; keep libc's LIFO semantics */
    }
}

_Noreturn void rt_exit(int status) {
    run_exit_handlers();
    rt_log("[shutdown] application exit handlers complete; terminating status %d", status);
    fflush(NULL);
    /* Diagnostic: leave framework workers alive after our cleanup. This distinguishes safe
     * finalizer avoidance from merely killing the process before a delayed graphics crash. */
    const char* delay = getenv("SR_SHUTDOWN_DELAY_MS");
    long ms = delay ? strtol(delay, NULL, 10) : 0;
    if (ms > 0 && ms <= 5000) {
        struct timespec wait = {ms / 1000, (ms % 1000) * 1000000};
        thrd_sleep(&wait, NULL);
    }
    /* _Exit keeps crash handling active throughout real cleanup. It avoids __cxa_finalize(NULL)
     * racing libhwui/vendor threads (reproduced FORTIFY destroyed-mutex SIGABRT after exit(0)). */
    _Exit(status);
}
#else
int rt_atexit(void (*handler)(void)) { return atexit(handler); }
_Noreturn void rt_exit(int status) { exit(status); }
#endif
