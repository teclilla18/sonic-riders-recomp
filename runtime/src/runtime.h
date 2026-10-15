/* Shared declarations of the native runtime. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ppc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* log.c */
void rt_log(const char* fmt, ...);
_Noreturn void rt_fatal(const char* fmt, ...);

/* memory.c: the guest physical window (see ppc.h) */
#define MEM1_SIZE 0x01800000u
#define LOCKED_CACHE_PHYS 0x20000000u
#define LOCKED_CACHE_SIZE 0x4000u
void mem_init(void);
static inline uint8_t* mem_phys(uint32_t pa) { return ppc_mem_base_var + (pa & PPC_PHYS_MASK); }
void mem_write_bytes(uint32_t ea, const void* src, size_t n);
void mem_read_bytes(uint32_t ea, void* dst, size_t n);
static inline uint32_t mem_r32(uint32_t ea) { return ld32(ea); }
static inline void mem_w32(uint32_t ea, uint32_t v) { st32(ea, v); }

/* disc.c: a GameCube disc image (GCM/ISO) */
int disc_open(const char* path);
size_t disc_read(uint64_t offset, void* dst, size_t n);
const uint8_t* disc_header(void);   /* first 0x440 bytes */
int disc_find(const char* path, uint32_t* offset, uint32_t* size);  /* FST lookup, 1 if found */
const uint8_t* disc_fst(uint32_t* size);

/* loader.c */
void load_game(void);   /* DOL sections, boot info, FST, the REL module linked at g_rel_base */

/* cpu.c */
void cpu_init(Cpu* c);
uint64_t rt_ticks(void);  /* guest timebase ticks since boot (40.5 MHz) */

#ifdef __cplusplus
}
#endif
