/* Guest memory: a reserved window indexed by guest physical address (ppc.h). Only MEM1 and the
 * locked cache are backed. Host: VirtualAlloc / mmap. Switch: the platform layer will map heap pages
 * into a code-memory reservation, as SwitchWakerHD does (platform/switch). */
#include <string.h>

#include "runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__SWITCH__)
#include <sys/mman.h>
#endif

uint8_t* ppc_mem_base_var;

#define WINDOW_SIZE (LOCKED_CACHE_PHYS + LOCKED_CACHE_SIZE)

#ifndef __SWITCH__
void mem_init(void) {
#ifdef _WIN32
    uint8_t* w = VirtualAlloc(NULL, WINDOW_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (!w) rt_fatal("cannot reserve the guest window");
    if (!VirtualAlloc(w, GUEST_RAM_SIZE, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(w + LOCKED_CACHE_PHYS, LOCKED_CACHE_SIZE, MEM_COMMIT, PAGE_READWRITE))
        rt_fatal("cannot back guest memory");
#else
    uint8_t* w = mmap(NULL, WINDOW_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (w == MAP_FAILED) rt_fatal("cannot reserve the guest window");
    if (mprotect(w, GUEST_RAM_SIZE, PROT_READ | PROT_WRITE) ||
        mprotect(w + LOCKED_CACHE_PHYS, LOCKED_CACHE_SIZE, PROT_READ | PROT_WRITE))
        rt_fatal("cannot back guest memory");
#endif
    ppc_mem_base_var = w;
    rt_log("[mem] guest window at %p (MEM1 %u MiB + locked cache)", (void*)w, MEM1_SIZE >> 20);
}
#endif

void mem_write_bytes(uint32_t ea, const void* src, size_t n) { memcpy(mem_phys(ea), src, n); }
void mem_read_bytes(uint32_t ea, void* dst, size_t n) { memcpy(dst, mem_phys(ea), n); }
