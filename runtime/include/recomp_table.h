/* Tables emitted by tools/recomp/gcrecomp.py (build/gen/table.c). */
#pragma once
#include "ppc.h"

typedef struct { uint32_t addr; PpcFunc fn; } RecompEntry;
typedef struct { uint32_t addr; const char* name; PpcFunc orig; } RecompHle;
typedef struct { uint32_t lo, hi; } RecompRange;

#ifdef __cplusplus
extern "C" {
#endif
extern const RecompEntry g_recomp_funcs[];
extern const unsigned g_recomp_func_count;
extern const RecompHle g_recomp_hle[];
extern const unsigned g_recomp_hle_count;
extern const uint32_t g_recomp_entry_point;
extern const uint32_t g_rel_base, g_rel_bss, g_rel_bss_size, g_rel_prolog;
extern const RecompRange g_code_ranges[];
extern const unsigned g_code_range_count;
#ifdef __cplusplus
}
#endif
