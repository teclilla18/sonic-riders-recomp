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

void rt_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
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
    static _Atomic uint64_t last;
    static int sync = -1;  /* SR_LOGSYNC: flush every line (finding where a hang stops) */
    if (sync < 0) sync = getenv("SR_LOGSYNC") != NULL;
    uint64_t now = rt_ticks();
    if (sync || now - atomic_load(&last) >= 40500000ull) {
        atomic_store(&last, now);
        fflush(stderr);
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
    fputs("FATAL: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}
