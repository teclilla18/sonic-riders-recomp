/* Keep SD/stdio latency off native realtime producers. The writer is not a
 * managed game worker: it must outlive their shutdown and flush their last logs. */
#include <switch.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include "switch2_platform.h"
#include "log_queue.h"

static Switch2LogQueue queue;
static Mutex lock;
static Thread writer;
static _Atomic int enabled, stop;
static _Atomic uint64_t dropped, request, persisted;
void switch2_log_write_done(uint64_t write_ns, uint64_t flush_ns);

int switch2_log_enqueue(const char* text) {
    if (!atomic_load_explicit(&enabled, memory_order_acquire)) return 0;
    /* In contention or exhaustion drop the diagnostic record, never block the
     * producer. Accepted records retain FIFO order; drops are visible. */
    if (!mutexTryLock(&lock)) { atomic_fetch_add(&dropped, 1); return 1; }
    if (!switch2_log_push(&queue, text)) atomic_fetch_add(&dropped, 1);
    mutexUnlock(&lock);
    return 1;
}
static void log_writer(void* arg) {
    (void)arg;
    uint64_t written = 0, last_flush = platform_now_ns(), seen_dropped = 0;
    for (;;) {
        char text[LOG_RECORD_BYTES];
        uint64_t seq;
        mutexLock(&lock);
        int have = switch2_log_pop(&queue, text, &seq);
        mutexUnlock(&lock);
        if (have) {
            uint64_t t = platform_now_ns();
            fputs(text, stderr); fputc('\n', stderr);
            switch2_log_write_done(platform_now_ns()-t, 0);
            written = seq;
        }
        uint64_t now = platform_now_ns();
        uint64_t req = atomic_load(&request);
        int flush_due = now-last_flush >= 1000000000ULL ||
                        (req > atomic_load(&persisted) && written >= req);
        if ((written > atomic_load(&persisted) && flush_due) || atomic_load(&stop)) {
            uint64_t lost = atomic_load(&dropped);
            if (lost != seen_dropped) {
                fprintf(stderr, "[log2-queue] dropped_records=%llu (bounded logging; gameplay producers never wait)\n",
                        (unsigned long long)lost);
                seen_dropped = lost;
            }
            uint64_t t = platform_now_ns();
            fflush(stderr);
            switch2_log_write_done(0, platform_now_ns()-t);
            atomic_store_explicit(&persisted, written, memory_order_release);
            last_flush = platform_now_ns();
        }
        if (!have) {
            if (atomic_load(&stop)) break;
            svcSleepThread(10000000);
        }
    }
}
void switch2_log_start(void) {
    /* Set permitted-core affinity before the thread can run. Logical core4
     * keeps the writer away from guest/render cores on the tested core mask. */
    Result rc = threadCreate(&writer, log_writer, NULL, NULL, 64*1024, 0x2d, -2);
    if (R_FAILED(rc)) { rt_log("[log2-queue] writer unavailable rc=%08X; synchronous fallback", rc); return; }
    /* Set explicit affinity before starting: logical4 maps only allowed cores. */
    u64 mask = 0;
    rc = svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_SUCCEEDED(rc) && mask) {
        unsigned nth = 4u % (unsigned)__builtin_popcountll(mask), core = 0;
        for (; core < 64; core++) if (mask & (1ULL << core)) { if (!nth--) break; }
        rc = svcSetThreadCoreMask(writer.handle, (s32)core, 1ULL << core);
    }
    if (R_FAILED(rc)) { threadClose(&writer); rt_log("[log2-queue] affinity failed rc=%08X; synchronous fallback", rc); return; }
    rc = threadStart(&writer);
    if (R_FAILED(rc)) { threadClose(&writer); rt_log("[log2-queue] start failed rc=%08X; synchronous fallback", rc); return; }
    atomic_store_explicit(&enabled, 1, memory_order_release);
    rt_log("[log2-queue] bounded background SD writer enabled, records=%u bytes=%u", LOG_QUEUE_COUNT, LOG_RECORD_BYTES);
}
void switch2_log_flush(void) {
    if (!atomic_load(&enabled)) { fflush(stderr); return; }
    mutexLock(&lock);
    uint64_t target = queue.tail;
    mutexUnlock(&lock);
    atomic_store(&request, target);
    while (atomic_load_explicit(&persisted, memory_order_acquire) < target) svcSleepThread(1000000);
}
void switch2_log_stop(void) {
    if (!atomic_load(&enabled)) return;
    switch2_log_flush();
    atomic_store(&stop, 1);
    Result rc = threadWaitForExit(&writer);
    if (R_FAILED(rc)) rt_fatal("native log writer join failed %08X", rc);
    threadClose(&writer);
    atomic_store(&enabled, 0);
}
