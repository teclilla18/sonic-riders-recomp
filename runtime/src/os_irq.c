/* Raise a Dolphin OS interrupt (__OS_INTERRUPT_*) the way __OSDispatchInterrupt would: unless the OS
 * or user mask has it masked, call the handler the game registered with __OSSetInterruptHandler
 * (InterruptHandlerTable at 0x80003040) with (interrupt, current context), in interrupt context.
 * Pending interrupts are latched one bit per source, as in the PI cause register: a source that
 * fires repeatedly while interrupts are disabled is delivered once when they are enabled again, and
 * a masked one stays pending until it is unmasked. */
#include <stdatomic.h>
#include <stdint.h>

#include "game_addrs.h"
#include "runtime.h"

#define OS_INTERRUPT_TABLE 0x80003040u

int dsp_irq_asserted(int id);  /* dsp.c */

static _Atomic uint32_t g_pending;  /* bit (0x80000000 >> id): raised, not delivered yet */
static _Atomic int g_posted;        /* a delivery item is queued */

static void dispatch_all(Cpu* ic, void* arg) {
    (void)arg;
    atomic_store(&g_posted, 0);
    uint32_t bits = atomic_exchange(&g_pending, 0);
    Cpu base = *ic;
    for (int id = 0; id < 32; id++) {
        uint32_t bit = 0x80000000u >> id;
        if (!(bits & bit)) continue;
        uint32_t mask = ld32(OS_INTERRUPT_MASK) | ld32(OS_USER_MASK);
        if (mask & bit) {
            atomic_fetch_or(&g_pending, bit);
            continue;
        }
        if (id >= 5 && id <= 7 && !dsp_irq_asserted(id)) continue;  /* acknowledged meanwhile: not pending any more */
        uint32_t handler = ld32(OS_INTERRUPT_TABLE + 4 * (uint32_t)id);
        if (id == 21) dbg_event(5, 0, 0);  /* DVD */
        if (!handler) continue;
        *ic = base;
        ic->r[3] = (uint32_t)id;
        ic->r[4] = ld32(OS_CURRENT_CONTEXT);
        guest_call(ic, handler);
    }
}

void os_raise_interrupt(int id) {
    atomic_fetch_or(&g_pending, 0x80000000u >> id);
    if (!atomic_exchange(&g_posted, 1)) rt_irq_post(dispatch_all, NULL);
}

void os_irq_dump(void) {
    rt_log("[irq] pending %08X posted %d, OS mask %08X user mask %08X", atomic_load(&g_pending), atomic_load(&g_posted),
           ld32(OS_INTERRUPT_MASK), ld32(OS_USER_MASK));
}

/* latched interrupts that were masked: try again (called when interrupts are re-enabled) */
void os_irq_retry(void) {
    uint32_t mask = ld32(OS_INTERRUPT_MASK) | ld32(OS_USER_MASK);
    if ((atomic_load(&g_pending) & ~mask) && !atomic_exchange(&g_posted, 1)) rt_irq_post(dispatch_all, NULL);
}
