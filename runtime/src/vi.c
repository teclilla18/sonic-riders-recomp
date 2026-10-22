/* Video Interface: a register model so the game's own VI library (VIInit, VIConfigure,
 * VIWaitForRetrace, retrace callbacks, __VIRetraceHandler) runs recompiled.
 * A host thread raises the retrace interrupt (DI0) at the NTSC field rate, 59.94 Hz. */
#include <string.h>
#include <threads.h>
#include <time.h>

#include "runtime.h"

#define OS_INTERRUPT_PI_VI 24

static volatile uint16_t g_vi[0x80];  /* 0xCC002000-0xCC0020FF, 16-bit registers */
static uint64_t g_field_start;        /* timebase tick of the current field's start */
static volatile uint32_t g_fields;
#define TICKS_PER_FIELD (40500000ull * 1001 / 60000)  /* 59.94 Hz */

uint32_t vi_read(uint32_t off, int size) {
    off &= 0xFF;
    if ((off & ~3u) == 0x2C) {  /* VCT (line) / HCT (pixel) of the beam */
        uint64_t t = rt_ticks() - g_field_start;
        uint32_t line = (uint32_t)(t * 263 / TICKS_PER_FIELD) + 1, pix = 1;
        if (line > 263) line = 263;
        uint32_t v = line << 16 | pix;
        return size == 4 ? v : (off & 2) ? (v & 0xFFFF) : (v >> 16);
    }
    if (size == 4) return (uint32_t)g_vi[off >> 1] << 16 | g_vi[(off >> 1) + 1];
    if (size == 2) return g_vi[off >> 1];
    return (off & 1) ? (g_vi[off >> 1] & 0xFF) : (g_vi[off >> 1] >> 8);
}

void vi_write(uint32_t off, uint32_t v, int size) {
    off &= 0xFF;
    if (size == 4) {
        g_vi[off >> 1] = (uint16_t)(v >> 16);
        g_vi[(off >> 1) + 1] = (uint16_t)v;
    } else if (size == 2) {
        g_vi[off >> 1] = (uint16_t)v;
    } else {
        uint16_t r = g_vi[off >> 1];
        g_vi[off >> 1] = (off & 1) ? (uint16_t)((r & 0xFF00) | (v & 0xFF)) : (uint16_t)((r & 0xFF) | (v << 8));
    }
}

/* the frame buffer the game asked VI to show next (top field), for presentation */
uint32_t vi_xfb_address(void) {
    uint32_t tfbl = (uint32_t)g_vi[0x1C >> 1] << 16 | g_vi[0x1E >> 1];
    uint32_t a = tfbl & 0x00FFFFFF;
    if (tfbl & 0x10000000u) a <<= 5;  /* page-offset bit: address in 32-byte units */
    return 0x80000000u | a;
}

uint32_t vi_field_count(void) { return g_fields; }

static int vi_thread(void* arg) {
    (void)arg;
    rt_thread_pin(2);
    g_field_start = rt_ticks();
    for (;;) {
        uint64_t next = g_field_start + TICKS_PER_FIELD;
        for (;;) {
            int64_t left = (int64_t)(next - rt_ticks());
            if (left <= 0) break;
            struct timespec ts = {0, (long)(left * 2000 / 81)};
            if (ts.tv_nsec > 2000000) ts.tv_nsec = 2000000;
            thrd_sleep(&ts, NULL);
        }
        g_field_start = next;
        g_fields++;
        { void gx_tl(char type, uint32_t a); gx_tl('V', g_fields); }
        /* DI0: the retrace interrupt VIInit configures (enable bit 0x1000, status bit 0x8000) */
        if (g_vi[0x30 >> 1] & 0x1000) {
            g_vi[0x30 >> 1] |= 0x8000;
            os_raise_interrupt(OS_INTERRUPT_PI_VI);
        }
    }
    return 0;
}

void vi_init(void) {
    memset((void*)g_vi, 0, sizeof g_vi);
    rt_thread_start(vi_thread, NULL);
}
