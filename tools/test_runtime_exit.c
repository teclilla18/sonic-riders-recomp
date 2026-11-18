/* Verify the actual runtime exit code in a child process, including buffered data and LIFO hooks. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rt_atexit(void (*handler)(void));
_Noreturn void rt_exit(int status);
void rt_log(const char* fmt, ...) { (void)fmt; puts("runtime-handlers-complete"); }
static void library_finalizer(void) { puts("process-wide-finalizer"); }
static void one(void) { fputs("1", stdout); }
static void four(void) { fputs("4", stdout); }
static void two(void) { fputs("2", stdout); assert(!rt_atexit(four)); }
static void three(void) { fputs("3", stdout); }
static void capacity(void) { fputs("x", stdout); }
int main(int argc, char** argv) {
    assert(!atexit(library_finalizer));
    setvbuf(stdout, NULL, _IOFBF, 4096);
    if (argc > 1 && !strcmp(argv[1], "capacity")) {
        assert(rt_atexit(NULL));
        for (int i = 0; i < 32; i++) assert(!rt_atexit(capacity));
        assert(rt_atexit(capacity));
        rt_exit(7);
    }
    assert(!rt_atexit(one)); assert(!rt_atexit(two)); assert(!rt_atexit(three));
    fputs("buffered:", stdout);
    rt_exit(0);
}
