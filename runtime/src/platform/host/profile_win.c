/* Sampling profiler for the desktop build (Windows), for finding the hot spots of the recompiled game
 * code and the runtime: SR_PROFILE=1 samples the instruction pointer of every thread that runs guest
 * code (registered by sched.c / main.c) about every millisecond; profile_dump() (the watchdog, or
 * exit) writes build/profile.txt as "offset-in-executable count" lines, which tools/profile_report.py
 * maps to functions with the linker map (build/host/sonicriders.map). Other platforms: no-ops. */
#include <stdio.h>
#include <stdlib.h>

#include "runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

#define MAX_THREADS 64
#define BUCKETS (1u << 20)  /* hashed offsets */

static HANDLE g_threads[MAX_THREADS];
static volatile LONG g_nthreads;
static int g_on = -1;
static uintptr_t g_base, g_size;
static struct { uint32_t off, count; } g_hist[BUCKETS];
static volatile LONG64 g_samples;
static int g_outside;  /* SR_PROFILE=2 */

static int profiling(void) {
    if (g_on < 0) {
        const char* e = getenv("SR_PROFILE");
        g_on = e != NULL;
        g_outside = e && atoi(e) == 2;
    }
    return g_on;
}

void profile_register_thread(void) {
    if (!profiling()) return;
    HANDLE h;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0))
        return;
    LONG i = InterlockedIncrement(&g_nthreads) - 1;
    if (i < MAX_THREADS) g_threads[i] = h;
}

static void add(uint32_t off) {
    uint32_t k = (off * 2654435761u) & (BUCKETS - 1);
    for (uint32_t n = 0; n < BUCKETS; n++, k = (k + 1) & (BUCKETS - 1)) {
        if (g_hist[k].count == 0) g_hist[k].off = off;
        if (g_hist[k].off == off) {
            g_hist[k].count++;
            return;
        }
    }
}

static DWORD WINAPI sampler(LPVOID arg) {
    (void)arg;
    timeBeginPeriod(1);
    for (;;) {
        Sleep(1);
        LONG n = g_nthreads < MAX_THREADS ? g_nthreads : MAX_THREADS;
        for (LONG i = 0; i < n; i++) {
            HANDLE h = g_threads[i];
            if (SuspendThread(h) == (DWORD)-1) continue;
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(h, &ctx)) {
                uintptr_t ip = (uintptr_t)ctx.Rip;
                if (ip >= g_base && ip < g_base + g_size) {  /* running in the executable */
                    add((uint32_t)(ip - g_base));
                    InterlockedIncrement64(&g_samples);
                } else if (g_outside) {
                    /* SR_PROFILE=2: time in Windows/C-library DLLs goes to the first executable frame
                     * that called it (tagged with the top bit), found by unwinding the stack */
                    for (int depth = 0; depth < 48; depth++) {
                        DWORD64 img;
                        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &img, NULL);
                        if (!fe) {  /* leaf function: the return address is at [rsp] */
                            ctx.Rip = *(DWORD64*)ctx.Rsp;
                            ctx.Rsp += 8;
                        } else {
                            void* hd;
                            DWORD64 est;
                            RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, ctx.Rip, fe, &ctx, &hd, &est, NULL);
                        }
                        ip = (uintptr_t)ctx.Rip;
                        if (!ip) break;
                        if (ip >= g_base && ip < g_base + g_size) {
                            add((uint32_t)(ip - g_base) | 0x80000000u);
                            InterlockedIncrement64(&g_samples);
                            break;
                        }
                    }
                }
            }
            ResumeThread(h);
        }
    }
    return 0;
}

void profile_start(void) {
    if (!profiling()) return;
    HMODULE m = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)m;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((uint8_t*)m + dos->e_lfanew);
    g_base = (uintptr_t)m;
    g_size = nt->OptionalHeader.SizeOfImage;
    CloseHandle(CreateThread(NULL, 0, sampler, NULL, 0, NULL));
    rt_log("[profile] sampling the game threads (SR_PROFILE)");
}

void profile_dump(void) {
    if (!profiling()) return;
    FILE* f = fopen("build/profile.txt", "w");
    if (!f) return;
    for (uint32_t k = 0; k < BUCKETS; k++)
        if (g_hist[k].count) fprintf(f, "%X %u\n", g_hist[k].off, g_hist[k].count);
    fclose(f);
    rt_log("[profile] %lld samples written to build/profile.txt (tools/profile_report.py)", (long long)g_samples);
}
#else
void profile_register_thread(void) {}
void profile_start(void) {}
void profile_dump(void) {}
#endif
