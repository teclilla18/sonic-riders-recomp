/* Shared declarations of the native runtime. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ppc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* platform layer (platform/<target>/platform_*.c) */
void platform_init(void);                   /* logging, file system, clocks */
const char* platform_disc_path(void);       /* default location of the game's disc image */
const char* platform_save_path(void);       /* memory card image for slot A */
void rt_thread_start(int (*fn)(void*), void* arg);  /* a host thread with a large stack (guest code is deep) */
void rt_thread_pin(int core);  /* run the calling thread on CPU core `core` (Switch: 0 guest, 1 render, 2 timers) */

/* log.c */
void rt_log(const char* fmt, ...);
_Noreturn void rt_fatal(const char* fmt, ...);

/* memory.c: the guest physical window (see ppc.h) */
#define MEM1_SIZE 0x01800000u        /* what the game sees */
#define GUEST_RAM_SIZE 0x01C00000u   /* backed: MEM1 + room above it for the REL (see gcrecomp.py) */
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

/* sched.c: guest threads and interrupts */
#define RT_IRQ_DEC    1u   /* decrementer: OSAlarm */
#define RT_IRQ_NATIVE 2u   /* native device callbacks queued with rt_irq_post */
void sched_init(void);
void sched_enter_boot_thread(void);
void rt_irq_raise(uint32_t bits);
void rt_irq_post(void (*fn)(Cpu* c, void* arg), void* arg);  /* run fn in interrupt context */
void guest_call(Cpu* c, uint32_t addr);                       /* call a guest function on c */
void sched_interrupts_enabled(Cpu* c);
void sched_set_decrementer(uint32_t v);
uint32_t sched_get_decrementer(void);

/* os_irq.c: raise __OS_INTERRUPT_<id> (runs the game's registered handler in interrupt context) */
void os_raise_interrupt(int id);
void os_irq_retry(void);

/* vi.c: Video Interface register model and retrace timer */
void vi_init(void);
uint32_t vi_read(uint32_t ea, int size);
void vi_write(uint32_t ea, uint32_t v, int size);
uint32_t vi_xfb_address(void);
uint32_t vi_field_count(void);

/* di.c: DVD Interface (drive) model */
uint32_t di_read(uint32_t ea, int size);
void di_write(uint32_t ea, uint32_t v, int size);
unsigned long long di_bytes_read(void);
unsigned long long di_read_ticks(void);
void sched_stats(uint64_t* idle, uint64_t* late_avg, uint64_t* late_max);  /* ticks; resets */  /* host time spent reading the disc image (timebase ticks) */

/* dsp.c: DSP mailboxes + boot ROM, ARAM and its DMA, AI DMA clock; ai_*: Audio Interface */
void dsp_init(void);
uint32_t dsp_read(uint32_t ea, int size);
void dsp_write(uint32_t ea, uint32_t v, int size);
uint32_t ai_read(uint32_t ea, int size);
void ai_write(uint32_t ea, uint32_t v, int size);
uint8_t* aram_ptr(uint32_t addr);

/* si.c: Serial Interface (controller ports) */
uint32_t si_read(uint32_t ea, int size);
void si_write(uint32_t ea, uint32_t v, int size);

/* input (platform): the state of GameCube controller `chan`, from the host's keyboard/gamepads */
typedef struct GcPad {
    uint16_t buttons;            /* PAD_BUTTON_* bits: LEFT 0x1 RIGHT 0x2 DOWN 0x4 UP 0x8 Z 0x10 R 0x20 L 0x40
                                    A 0x100 B 0x200 X 0x400 Y 0x800 START 0x1000 */
    uint8_t stick_x, stick_y;    /* 0x80 = centre */
    uint8_t c_x, c_y;
    uint8_t trig_l, trig_r;
} GcPad;
void input_get_pad(int chan, GcPad* out);
void input_set_rumble(int chan, int motor);

/* exi.c: EXI bus: memory card (slot A), IPL SRAM/RTC */
void exi_init(const char* card_path);
uint32_t exi_read(uint32_t ea, int size);
void exi_write(uint32_t ea, uint32_t v, int size);

/* ax.c: AX microcode stand-in and host audio output */
void ax_process_command_list(uint32_t addr, uint32_t size);
void audio_submit(uint32_t addr, uint32_t bytes);
unsigned long long ax_list_count(void);

#ifdef __cplusplus
}
#endif
