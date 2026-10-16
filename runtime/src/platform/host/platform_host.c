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

void platform_init(void) {}

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
