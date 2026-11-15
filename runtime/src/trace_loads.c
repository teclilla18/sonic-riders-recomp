/* Guest load tracer (debug builds only: configure.py --trace-loads defines PPC_TRACE_LOADS).
 *
 * SR_TRACE_LOADS=LO,LEN[,STRIDE] (hex): every guest load from [LO, LO+LEN) is recorded as
 * (guest function last entered and its return address, offset, size). A load made after a call returned is
 * charged to the callee, so then the real reader is the function holding the return address. With STRIDE the offset is taken modulo STRIDE, so an array
 * of structs (e.g. the 8 racers) folds into one struct's field offsets. A helper thread writes the table to
 * SR_TRACE_LOADS_FILE (default trace_loads.txt) every 5 s: "function offset size count".
 * Used to find every piece of code that reads a field or table (e.g. the racer's character id), which a
 * normal run cannot show. Nothing here runs in normal builds. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t g_tl_lo, g_tl_len = 0;
static uint32_t g_tl_stride;
static int g_tl_names;
/* SR_TRACE_LOADS_FN=<hex>[-<hex end>]: only loads made while that guest function was entered last, or (with a
 * code range) while the function entered last returns into that range (the reader had called something) */
static uint32_t g_tl_fn, g_tl_fn_end;

#define TL_SLOTS 16384
typedef struct { uint32_t fn, lr, off, size, count; } TlEntry;
static TlEntry g_tl[TL_SLOTS];
static volatile int g_tl_lock;
static unsigned g_tl_used;

void ppc_trace_load(uint32_t ea, uint32_t size) {
    extern volatile uint32_t g_ppc_cur_fn, g_ppc_cur_lr;
    uint32_t off = ea - g_tl_lo;
    if (g_tl_stride) off %= g_tl_stride;
    uint32_t fn = g_ppc_cur_fn, lr = g_ppc_cur_lr;
    if (g_tl_fn && fn != g_tl_fn &&
        !(g_tl_fn_end && ((fn >= g_tl_fn && fn < g_tl_fn_end) || (lr >= g_tl_fn && lr < g_tl_fn_end))))
        return;
    if (g_tl_names && size == 1) {
        /* SR_TRACE_LOADS_NAMES=1 (with _FN = the path lookup): log each string it reads the first time,
         * with the last REL functions entered (who built the name) */
        uint32_t phys = ea & 0x3FFFFFFFu;
        const char* s = (const char*)(PPC_MEM_BASE + phys);
        if (phys && s[-1] == 0 && s[0] >= 0x20) {
            static char seen[256][24];
            static int nseen;
            int dup = 0;
            for (int i = 0; i < nseen && !dup; i++) dup = !strncmp(seen[i], s, 23);
            if (!dup && nseen < 256) {
                strncpy(seen[nseen++], s, 23);
                int cpu_trace_recent(uint32_t* out, int n);
                static uint32_t r[4096];
                int n = cpu_trace_recent(r, 4096), k = 0;
                char line[512];
                int len = snprintf(line, sizeof line, "[trace-loads] name \"%.23s\" at %08X; REL functions:", s, ea);
                for (int i = 0; i < n && k < 10 && len < 480; i++)
                    if (r[i] >= 0x81800000u) { len += snprintf(line + len, sizeof line - len, " %08X", r[i]); k++; }
                rt_log("%s", line);
            }
        }
    }
    uint32_t h = (fn * 2654435761u) ^ (lr * 2246822519u) ^ (off * 40503u) ^ size;
    while (__atomic_exchange_n(&g_tl_lock, 1, __ATOMIC_ACQUIRE)) { }
    for (unsigned i = 0; i < TL_SLOTS; i++) {
        TlEntry* e = &g_tl[(h + i) % TL_SLOTS];
        if (e->count && e->fn == fn && e->lr == lr && e->off == off && e->size == size) { e->count++; break; }
        if (!e->count) {
            if (g_tl_used < TL_SLOTS - 64) { e->fn = fn; e->lr = lr; e->off = off; e->size = size; e->count = 1; g_tl_used++; }
            break;
        }
    }
    __atomic_store_n(&g_tl_lock, 0, __ATOMIC_RELEASE);
}

static int tl_writer(void* arg) {
    const char* path = (const char*)arg;
    static TlEntry snap[TL_SLOTS];
    for (;;) {
        platform_sleep_ns(5000000000ull);
        while (__atomic_exchange_n(&g_tl_lock, 1, __ATOMIC_ACQUIRE)) { }
        memcpy(snap, g_tl, sizeof snap);
        __atomic_store_n(&g_tl_lock, 0, __ATOMIC_RELEASE);
        FILE* f = fopen(path, "w");
        if (!f) continue;
        fprintf(f, "# SR_TRACE_LOADS %08X,%X,%X: function its-return-address offset size count\n", g_tl_lo, g_tl_len,
                g_tl_stride);
        for (unsigned i = 0; i < TL_SLOTS; i++)
            if (snap[i].count)
                fprintf(f, "%08X %08X %X %u %u\n", snap[i].fn, snap[i].lr, snap[i].off, snap[i].size, snap[i].count);
        fclose(f);
    }
    return 0;
}

void trace_loads_init(void) {
    const char* s = getenv("SR_TRACE_LOADS");
    if (!s) return;
#ifndef PPC_TRACE_LOADS
    rt_log("[trace-loads] SR_TRACE_LOADS needs a build made with configure.py --trace-loads; ignored");
#else
    unsigned lo = 0, len = 0, stride = 0;
    if (sscanf(s, "%x,%x,%x", &lo, &len, &stride) < 2 || !len) {
        rt_log("[trace-loads] SR_TRACE_LOADS=LO,LEN[,STRIDE] (hex); ignored: %s", s);
        return;
    }
    g_tl_stride = stride;
    if (getenv("SR_TRACE_LOADS_FN")) {
        char* end;
        g_tl_fn = (uint32_t)strtoul(getenv("SR_TRACE_LOADS_FN"), &end, 16);
        if (*end == '-') g_tl_fn_end = (uint32_t)strtoul(end + 1, NULL, 16);
    }
    if (getenv("SR_TRACE_LOADS_NAMES")) {
        extern int g_ppc_trace;
        g_tl_names = 1;
        g_ppc_trace = 1;  /* the function-entry ring (cpu.c) */
    }
    g_tl_lo = lo;
    g_tl_len = len;  /* last: turns it on */
    const char* path = getenv("SR_TRACE_LOADS_FILE");
    rt_thread_start(tl_writer, (void*)(path ? path : "trace_loads.txt"));
    rt_log("[trace-loads] recording loads from %08X..%08X (stride %X)", lo, lo + len, stride);
#endif
}
