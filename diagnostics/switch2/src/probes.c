/* SPDX-License-Identifier: MIT */
#include "diag.h"
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool rc_ok(Result rc, const char *name) {
    diag_log("%s rc=0x%08x", name, rc);
    return R_SUCCEEDED(rc);
}

void probe_services(void) {
    bool ok = true;
    u64 cores = 0, memory = 0;
    diag_log("BEGIN svcGetInfo CoreMask/TotalMemorySize");
    ok &= rc_ok(svcGetInfo(&cores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0), "CoreMask");
    ok &= rc_ok(svcGetInfo(&memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0), "TotalMemorySize");
    diag_log("Process allowed cores=0x%llx total_memory=%llu applet_mode=%d", (unsigned long long)cores, (unsigned long long)memory, appletGetOperationMode());
    diag_log("BEGIN setsysInitialize");
    Result rc = setsysInitialize();
    if (rc_ok(rc, "setsysInitialize")) {
        SetSysFirmwareVersion fw = {0};
        rc = setsysGetFirmwareVersion(&fw);
        ok &= rc_ok(rc, "setsysGetFirmwareVersion");
        if (R_SUCCEEDED(rc)) diag_log("Firmware %u.%u.%u (%s)", fw.major, fw.minor, fw.micro, fw.display_version);
        setsysExit();
    } else ok = false;
    diag_log("BEGIN romfsInit (embedded build manifest)");
    rc = romfsInit();
    if (rc_ok(rc, "romfsInit")) {
        FILE *f = fopen("romfs:/build-info.json", "rb");
        char buf[1024] = {0};
        if (f) { size_t n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); ok &= n > 0 && strstr(buf, "sr2diag-build-v1") != NULL; }
        else ok = false;
        romfsExit();
    } else ok = false;
    diag_log("BEGIN socketInitializeDefault (no network connection is made)");
    rc = socketInitializeDefault();
    ok &= rc_ok(rc, "socketInitializeDefault");
    if (R_SUCCEEDED(rc)) socketExit();
    diag_log("BEGIN SD write / flush / seek / read verification");
    const char *path = "sdmc:/switch/SR2Diag/io-test.tmp";
    FILE *f = fopen(path, "wb+");
    const char expected[] = "SR2Diag file round trip v1";
    char actual[sizeof expected] = {0};
    if (f) {
        bool io = fwrite(expected, 1, sizeof expected, f) == sizeof expected;
        io &= fflush(f) == 0;
        io &= fsync(fileno(f)) == 0;
        io &= fseek(f, 0, SEEK_SET) == 0;
        io &= fread(actual, 1, sizeof actual, f) == sizeof actual;
        io &= memcmp(actual, expected, sizeof actual) == 0;
        diag_log("SD round trip=%d errno=%d", io, errno);
        ok &= io;
        fclose(f);
        remove(path);
    } else { diag_log("SD open failed errno=%d", errno); ok = false; }
    result(T_SERVICES, ok ? PASS : FAIL, ok ? "service queries, embedded manifest and flushed SD I/O passed" : "one or more service/I/O calls failed; see exact results in log");
}

void probe_timing(void) {
    diag_log("BEGIN CLOCK_MONOTONIC and SDK physical counter (cntpct_el0/cntfrq_el0)");
    uint64_t a = monotonic_ns();
    if (!a) { result(T_TIMING, FAIL, "CLOCK_MONOTONIC unavailable"); return; }
    uint64_t freq = armGetSystemTickFreq();
    uint64_t tick_a = armGetSystemTick();
    svcSleepThread(100000000);
    uint64_t tick_b = armGetSystemTick(), b = monotonic_ns();
    uint64_t converted = armTicksToNs(tick_b - tick_a);
    uint64_t measured = b - a;
    bool ok = b > a && tick_b > tick_a && freq == 31250000ULL;
    ok &= measured >= 80000000ULL && measured < 3000000000ULL;
    uint64_t difference = converted > measured ? converted - measured : measured - converted;
    ok &= difference < measured / 10 + 2000000;
    for (int i = 0; i < 1000; i++) {
        uint64_t now = monotonic_ns();
        if (!now || now < b) ok = false;
        b = now;
    }
    diag_log("Timing freq=%llu monotonic_delta_ns=%llu SDK_delta_ns=%llu; direct cntvct_el0 deliberately NOT tested",
             (unsigned long long)freq, (unsigned long long)measured, (unsigned long long)converted);
    result(T_TIMING, ok ? PASS : FAIL, ok ? "monotonic clock + SDK counter agree; 31.25 MHz observed" : "clock/frequency/monotonicity check failed");
}

typedef struct {
    Thread thread;
    atomic_bool done;
    bool ok;
    unsigned core;
    s32 preferred;
    u64 affinity;
} Worker;
static _Thread_local unsigned tls_cookie;

static void worker_entry(void *arg) {
    Worker *w = arg;
    tls_cookie = 0x53523244;
    unsigned char *buf = malloc(65536);
    w->ok = buf != NULL;
    if (buf) {
        memset(buf, 0x5a, 65536);
        for (int i = 0; i < 65536; i++) if (buf[i] != 0x5a) w->ok = false;
        free(buf);
    }
    w->core = svcGetCurrentProcessorNumber();
    w->ok &= R_SUCCEEDED(svcGetThreadCoreMask(&w->preferred, &w->affinity, CUR_THREAD_HANDLE));
    w->ok &= tls_cookie == 0x53523244;
    atomic_store_explicit(&w->done, true, memory_order_release);
}

void probe_threads(void) {
    u64 mask = 0;
    diag_log("BEGIN query actual process CPU mask");
    if (!rc_ok(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0), "process CoreMask") || !mask) {
        result(T_THREADS, FAIL, "cannot query allowed core mask"); return;
    }
    bool ok = true;
    tls_cookie = 0x4d41494e;
    s32 priority = 0;
    if (!rc_ok(svcGetThreadPriority(&priority, CUR_THREAD_HANDLE), "main priority")) {
        result(T_THREADS, FAIL, "cannot query main priority"); return;
    }
    for (unsigned core = 0; core < 64; core++) {
        if (!(mask & (1ULL << core))) continue;
        Worker *w = calloc(1, sizeof *w);
        if (!w) { ok = false; break; }
        atomic_init(&w->done, false);
        diag_log("BEGIN threadCreate requested_core=%u stack=262144 priority=%d", core, priority);
        Result rc = threadCreate(&w->thread, worker_entry, w, NULL, 262144, priority, (int)core);
        if (!rc_ok(rc, "threadCreate")) { free(w); ok = false; continue; }
        rc = svcSetThreadCoreMask(w->thread.handle, (s32)core, 1ULL << core);
        bool affinity_ok = rc_ok(rc, "worker set permitted core mask");
        rc = threadStart(&w->thread);
        if (!rc_ok(rc, "threadStart")) {
            bool closed = rc_ok(threadClose(&w->thread), "close unstarted worker");
            if (closed) free(w);
            else cleanup_failed = true;
            ok = false;
            if (!closed) break;
            continue;
        }
        /* Kernel wait yields even when main and worker share an equal-priority core. */
        rc = svcWaitSynchronizationSingle(w->thread.handle, 3000000000ULL);
        if (!rc_ok(rc, "bounded thread completion wait")) {
            /* Do not free a live thread's state/stack. No further probes are safe. */
            cleanup_failed = true;
            result(T_THREADS, FAIL, "worker did not finish; live state retained, stop testing");
            return;
        }
        rc = threadWaitForExit(&w->thread);
        bool joined = rc_ok(rc, "threadWaitForExit");
        bool done = atomic_load_explicit(&w->done, memory_order_acquire);
        bool close_ok = rc_ok(threadClose(&w->thread), "threadClose");
        diag_log("Worker requested=%u actual=%u affinity=0x%llx done=%d tls/alloc=%d",
                 core, w->core, (unsigned long long)w->affinity, done, w->ok);
        ok &= affinity_ok && joined && close_ok && done && w->ok && w->core == core;
        ok &= tls_cookie == 0x4d41494e;
        if (!close_ok) { cleanup_failed = true; break; }
        free(w);
    }
    result(T_THREADS, ok ? PASS : FAIL, ok ? "allocation, TLS isolation, permitted core affinity and join passed" : "worker/TLS/affinity check failed; see log");
}

void probe_input(void) {
    const u64 needed = HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                       HidNpadButton_L | HidNpadButton_R | HidNpadButton_Up | HidNpadButton_Down |
                       HidNpadButton_Left | HidNpadButton_Right;
    u64 seen = 0;
    bool sticks[2] = {false, false}, connected = false;
    uint64_t start = monotonic_ns();
    diag_log("BEGIN 30 second input sample; required button mask=0x%llx", (unsigned long long)needed);
    while (poll_input() && monotonic_ns() - start < 30000000000ULL) {
        u64 down = padGetButtonsDown(&pad);
        seen |= padGetButtons(&pad);
        connected |= padIsConnected(&pad);
        for (int i = 0; i < 2; i++) {
            HidAnalogStickState s = padGetStickPos(&pad, (unsigned)i);
            if (abs(s.x) > 8000 || abs(s.y) > 8000) sticks[i] = true;
        }
        if (down) diag_log("Input edge=0x%llx observed=0x%llx sticks=%d,%d connected=%d",
                           (unsigned long long)down, (unsigned long long)seen, sticks[0], sticks[1], connected);
        redraw("Press A B X Y, L R, all D-pad directions; move BOTH sticks. + exits.");
        if (connected && (seen & needed) == needed && sticks[0] && sticks[1]) break;
        svcSleepThread(16000000);
    }
    diag_log("Input sample final buttons=0x%llx sticks=%d,%d connected=%d", (unsigned long long)seen, sticks[0], sticks[1], connected);
    bool ok = connected && (seen & needed) == needed && sticks[0] && sticks[1];
    result(T_INPUT, quitting ? SKIPPED : ok ? PASS : FAIL,
           quitting ? "cancelled by exit" : ok ? "buttons and both analog sticks sampled" : "required inputs not all observed in 30 seconds");
}

void probe_audio(void) {
    diag_log("BEGIN audoutInitialize");
    if (!rc_ok(audoutInitialize(), "audoutInitialize")) {
        result(T_AUDIO, FAIL, "audio service initialization failed"); return;
    }
    u32 rate = audoutGetSampleRate(), channels = audoutGetChannelCount();
    PcmFormat format = audoutGetPcmFormat();
    diag_log("Audio rate=%u channels=%u format=%d", rate, channels, format);
    bool ok = rate == 48000 && channels == 2 && format == PcmFormat_Int16;
    const size_t frames = 24000, bytes = frames * 2 * sizeof(int16_t);
    size_t allocation = (bytes + 4095) & ~(size_t)4095;
    int16_t *samples = aligned_alloc(4096, allocation);
    if (!samples) ok = false;
    if (ok) {
        memset(samples, 0, allocation);
        for (size_t i = 0; i < frames; i++) {
            /* Two quiet tones, with fade envelopes to avoid clicks. */
            unsigned channel = i < frames / 2 ? 0 : 1;
            size_t local = i % (frames / 2);
            double fade = fmin(1.0, fmin((double)local / 480, (double)(frames / 2 - 1 - local) / 480));
            samples[2 * i + channel] = (int16_t)(2400 * fade * sin(6.283185307179586 * (channel ? 660 : 440) * (double)i / rate));
        }
        armDCacheFlush(samples, allocation);
        AudioOutBuffer buffer = {.buffer = samples, .buffer_size = allocation, .data_size = bytes};
        diag_log("BEGIN audio append/start; listen for left low tone then right higher tone");
        ok &= rc_ok(audoutAppendAudioOutBuffer(&buffer), "audoutAppendAudioOutBuffer");
        if (ok) ok &= rc_ok(audoutStartAudioOut(), "audoutStartAudioOut");
        if (ok) {
            AudioOutBuffer *released = NULL;
            u32 count = 0;
            Result rc = audoutWaitPlayFinish(&released, &count, 3000000000ULL);
            ok &= rc_ok(rc, "audio completion (3 second timeout)");
            ok &= count > 0 && released == &buffer;
            diag_log("Audio released_count=%u expected_buffer=%d", count, released == &buffer);
        }
        bool stopped = rc_ok(audoutStopAudioOut(), "audoutStopAudioOut");
        bool flushed = false;
        Result rc = audoutFlushAudioOutBuffers(&flushed);
        diag_log("audoutFlushAudioOutBuffers rc=0x%08x flushed=%d", rc, flushed);
        ok &= stopped && R_SUCCEEDED(rc);
    }
    diag_log("BEGIN audoutExit before releasing PCM memory");
    audoutExit();
    free(samples);
    diag_log("END audio transport; internal checks=%d", ok);
    if (!ok) { result(T_AUDIO, FAIL, "audio transport failed; inspect service result codes"); return; }
    Status heard = confirm("Did you hear the two tones? Y yes / B no / X skip (30 seconds)");
    result(T_AUDIO, heard, heard == PASS ? "transport passed and tester confirmed audible tones" : heard == FAIL ? "transport passed but tester heard no tones" : "transport passed; audible output not confirmed");
}

void probe_mapping(bool code) {
    int id = code ? T_CODE_MAP : T_STACK_MAP;
    Handle self = envGetOwnProcessHandle();
    if (code && (self == INVALID_HANDLE || !envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x78) || !envIsSyscallHinted(0x73))) {
        result(id, SKIPPED, "process handle or required code-map/unmap/permission hints unavailable"); return;
    }
    const size_t window_size = 0x20010000ULL;
    const size_t sizes[2] = {0x01c00000, 0x10000};
    const size_t offsets[2] = {0, 0x20000000};
    void *backing[2] = {NULL, NULL};
    bool mapped[2] = {false, false};
    diag_log("BEGIN reserve guest VA window size=0x%zx; physical backing=28MiB+64KiB; strategy=%s", window_size, code ? "process-code" : "stack");
    virtmemLock();
    uint8_t *window = code ? virtmemFindCodeMemory(window_size, 0x10000) : virtmemFindStack(window_size, 0x10000);
    VirtmemReservation *reservation = window ? virtmemAddReservation(window, window_size) : NULL;
    virtmemUnlock();
    if (!reservation) { result(id, FAIL, "cannot reserve 512MiB+64KiB of sparse guest VA"); return; }
    diag_log("Guest window=%p", (void *)window);
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        diag_log("BEGIN aligned backing allocation size=0x%zx", sizes[i]);
        backing[i] = aligned_alloc(0x1000, sizes[i]);
        if (!backing[i]) { ok = false; break; }
        memset(backing[i], 0, sizes[i]);
        diag_log("BEGIN map region=%d dst=%p src=%p size=0x%zx", i, (void *)(window + offsets[i]), backing[i], sizes[i]);
        Result rc = code ? svcMapProcessCodeMemory(self, (u64)(window + offsets[i]), (u64)backing[i], sizes[i])
                         : svcMapMemory(window + offsets[i], backing[i], sizes[i]);
        if (!rc_ok(rc, "guest map")) { ok = false; break; }
        mapped[i] = true;
        if (code && !rc_ok(svcSetProcessMemoryPermission(self, (u64)(window + offsets[i]), sizes[i], Perm_Rw), "guest set RW permission")) { ok = false; break; }
        diag_log("BEGIN mapped page write/read verification region=%d", i);
        volatile uint32_t *words = (volatile uint32_t *)(window + offsets[i]);
        for (size_t j = 0; j < sizes[i] / sizeof(uint32_t); j += 1024) words[j] = 0x53520000u ^ (uint32_t)j;
        words[sizes[i] / sizeof(uint32_t) - 1] = 0x5441494c;
        for (size_t j = 0; j < sizes[i] / sizeof(uint32_t); j += 1024)
            if (words[j] != (0x53520000u ^ (uint32_t)j)) ok = false;
        if (words[sizes[i] / sizeof(uint32_t) - 1] != 0x5441494c) ok = false;
        MemoryInfo info = {0}; u32 page = 0;
        rc = svcQueryMemory(&info, &page, (u64)(window + offsets[i]));
        ok &= rc_ok(rc, "query mapped permissions");
        if (R_SUCCEEDED(rc)) {
            diag_log("Mapped region type=0x%x perm=0x%x size=0x%llx", info.type, info.perm, (unsigned long long)info.size);
            ok &= (info.perm & Perm_Rw) == Perm_Rw;
        }
        if (!ok) break;
    }
    for (int i = 1; i >= 0; i--) {
        if (mapped[i]) {
            diag_log("BEGIN unmap region=%d", i);
            Result rc = code ? svcUnmapProcessCodeMemory(self, (u64)(window + offsets[i]), (u64)backing[i], sizes[i])
                             : svcUnmapMemory(window + offsets[i], backing[i], sizes[i]);
            if (!rc_ok(rc, "guest unmap")) { cleanup_failed = true; ok = false; continue; }
        }
        free(backing[i]);
    }
    if (!cleanup_failed) {
        virtmemLock(); virtmemRemoveReservation(reservation); virtmemUnlock();
        diag_log("END guest window released");
    } else diag_log("FAIL cleanup: still-mapped backing/reservation retained; stop testing");
    result(id, ok ? PASS : FAIL, ok ? "sparse guest ranges mapped, page-tested and fully unmapped" : "reservation/map/permission/test/cleanup failed; see checkpoint log");
}
