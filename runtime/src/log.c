/* Logging to stderr (on the Switch a file on the SD card, fully buffered: platform_switch.c).
 * The buffer is flushed at most once a second, on fatal errors and at exit: flushing every line
 * to the SD card made bursts of log lines (a save logged per card command) freeze the game. */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "runtime.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifdef __SWITCH2__
int switch2_log_enqueue(const char*);
void switch2_log_flush(void);
static _Atomic uint64_t g_log_write_max, g_log_flush_max, g_log_slow;
static void log_max(_Atomic uint64_t* value, uint64_t n) {
    uint64_t old = atomic_load_explicit(value, memory_order_relaxed);
    while (n > old && !atomic_compare_exchange_weak_explicit(value, &old, n, memory_order_relaxed, memory_order_relaxed)) {}
}
void switch2_log_write_done(uint64_t write_ns, uint64_t flush_ns) {
    log_max(&g_log_write_max, write_ns); log_max(&g_log_flush_max, flush_ns);
    if (write_ns > 50000000ULL || flush_ns > 50000000ULL) atomic_fetch_add(&g_log_slow, 1);
}
void switch2_log_stats(uint64_t* write_ns, uint64_t* flush_ns, uint64_t* slow) {
    *write_ns = atomic_exchange(&g_log_write_max, 0);
    *flush_ns = atomic_exchange(&g_log_flush_max, 0);
    *slow = atomic_exchange(&g_log_slow, 0);
}
#endif
void rt_log(const char* fmt, ...) {
#ifdef __SWITCH2__
    uint64_t write_start = platform_now_ns();
#endif
    va_list ap;
    va_start(ap, fmt);
#ifdef __SWITCH2__
    char record[1024];
    va_list copy; va_copy(copy, ap);
    vsnprintf(record, sizeof record, fmt, copy); va_end(copy);
    if (switch2_log_enqueue(record)) { va_end(ap); return; }
#endif
#ifdef __ANDROID__
    {   /* also to logcat (tag "sonicriders"): adb logcat follows the game live */
        char buf[1024];
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(buf, sizeof buf, fmt, ap2);
        va_end(ap2);
        __android_log_write(ANDROID_LOG_INFO, "sonicriders", buf);
    }
#elif defined(SR_IOS)
    {   /* stderr is the log file in Documents (platform_host.c); stdout reaches the Mac live
         * (tools/ios_run.py run: devicectl --console); flushed with the log below */
        va_list ap2;
        va_copy(ap2, ap);
        vfprintf(stdout, fmt, ap2);
        va_end(ap2);
        fputc('\n', stdout);
    }
#endif
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
#ifdef __SWITCH2__
    uint64_t write_ns = platform_now_ns() - write_start;
    log_max(&g_log_write_max, write_ns);
    if (write_ns > 50000000ULL) atomic_fetch_add(&g_log_slow, 1);
#endif
    static _Atomic uint64_t last;
    static int sync = -1;  /* SR_LOGSYNC: flush every line (finding where a hang stops) */
    if (sync < 0) sync = getenv("SR_LOGSYNC") != NULL;
    uint64_t now = rt_ticks();
    if (sync || now - atomic_load(&last) >= 40500000ull) {
        atomic_store(&last, now);
#ifdef __SWITCH2__
        uint64_t flush_start = platform_now_ns();
#endif
        fflush(stderr);
#ifdef __SWITCH2__
        uint64_t flush_ns = platform_now_ns() - flush_start;
        log_max(&g_log_flush_max, flush_ns);
        if (flush_ns > 50000000ULL) atomic_fetch_add(&g_log_slow, 1);
#endif
#ifdef SR_IOS
        fflush(stdout);
#endif
    }
}

_Noreturn void rt_fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    {
        char buf[1024];
        va_list ap2;
        va_copy(ap2, ap);
        vsnprintf(buf, sizeof buf, fmt, ap2);
        va_end(ap2);
        __android_log_print(ANDROID_LOG_FATAL, "sonicriders", "FATAL: %s", buf);
    }
#endif
#ifdef SR_IOS
    {
        va_list ap2;
        va_copy(ap2, ap);
        fputs("FATAL: ", stdout);
        vfprintf(stdout, fmt, ap2);
        va_end(ap2);
        fputc('\n', stdout);
        fflush(stdout);
    }
#endif
#ifdef __SWITCH2__
    switch2_log_flush();
#endif
    fputs("FATAL: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}
