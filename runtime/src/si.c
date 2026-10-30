/* Serial Interface (0xCC006400): the controller ports, modelled at the register level so the game's
 * own SI and PAD libraries (type detection, origin, polling, rumble) run recompiled. Port 1 always
 * holds a standard controller; ports 2-4 hold one while the host has a controller for that player
 * (input_connected), so the game sees controllers plugged in and out. All are fed by input_get_pad.
 * Registers and the controller protocol: YAGCD "SI", Dolphin's SerialInterface / SI_DeviceGCController. */
#include <string.h>

#include "runtime.h"

#define OS_INTERRUPT_PI_SI 20


/* SR_DETCLOCK (online play groundwork): the controllers are read once per game frame (si_det_latch, at
 * the frame end) and every poll in the next frame sees that state, so the game sees the same input
 * on the same frame in every run, whenever its own polls happen */
static GcPad g_det_latch[4] = {{0, 0x80, 0x80, 0x80, 0x80, 0, 0}, {0, 0x80, 0x80, 0x80, 0x80, 0, 0},
                               {0, 0x80, 0x80, 0x80, 0x80, 0, 0}, {0, 0x80, 0x80, 0x80, 0x80, 0, 0}};
void si_det_latch(void) {
    for (int ch = 0; ch < 4; ch++) input_get_pad(ch, &g_det_latch[ch]);
}
static void get_pad(int ch, GcPad* p) {
    int ppc_detclock(void);
    if (ppc_detclock()) *p = g_det_latch[ch & 3];
    else input_get_pad(ch, p);
}

static uint32_t g_out[4];        /* SICnOUTBUF: the poll command (0x40 0x03 motor) */
static uint32_t g_poll, g_comcsr, g_status, g_exilk;
static uint8_t g_buf[128];       /* SI I/O buffer for SITransfer */
static uint32_t g_inl_latched[4];

#define COMCSR_TCINT     0x80000000u
#define COMCSR_TCINTMSK  0x40000000u
#define COMCSR_RDSTINT   0x10000000u
#define COMCSR_RDSTINTMSK 0x08000000u

static int connected(int ch) { return ch == 0 || input_connected(ch); }

/* the controller's analog origin: neutral sticks, triggers released */
static void origin(uint8_t* r) {
    static const uint8_t o[10] = {0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x1F, 0x1F, 0x00, 0x00};
    memcpy(r, o, 10);
}

static void transfer(void) {
    int ch = (int)((g_comcsr >> 1) & 3);
    uint32_t out_len = (g_comcsr >> 16) & 0x7F, in_len = (g_comcsr >> 8) & 0x7F;
    if (!out_len) out_len = 128;
    if (!in_len) in_len = 128;
    uint8_t resp[128];
    memset(resp, 0, sizeof resp);
    int ok = connected(ch);
    if (ok) {
        switch (g_buf[0]) {
        case 0x00: case 0xFF:              /* type: standard controller */
            resp[0] = 0x09; resp[1] = 0x00; resp[2] = 0x00;
            break;
        case 0x41: case 0x42:              /* origin / recalibrate */
            origin(resp);
            break;
        case 0x40: {                       /* direct poll */
            GcPad p;
            get_pad(ch, &p);
            uint16_t b = p.buttons | 0x0080;
            resp[0] = (uint8_t)(b >> 8); resp[1] = (uint8_t)b;
            resp[2] = p.stick_x; resp[3] = p.stick_y; resp[4] = p.c_x; resp[5] = p.c_y;
            resp[6] = p.trig_l; resp[7] = p.trig_r;
            break;
        }
        default:
            rt_log("[si] unhandled controller command %02X", g_buf[0]);
            break;
        }
        memcpy(g_buf, resp, in_len);
        g_status &= ~(0x08000000u >> (8 * ch));          /* NOREP clear */
    } else {
        g_status |= 0x08000000u >> (8 * ch);             /* no response */
    }
    (void)out_len;
    g_comcsr &= ~1u;
    g_comcsr |= COMCSR_TCINT;
    if (!ok) g_comcsr |= 0x20000000u;                    /* COMERR */
    else g_comcsr &= ~0x20000000u;
    if (g_comcsr & COMCSR_TCINTMSK) os_raise_interrupt(OS_INTERRUPT_PI_SI);
}

/* polled input: the response of channel `ch` to its poll command, as INBUFH / INBUFL */
static void poll_response(int ch, uint32_t* hi, uint32_t* lo) {
    GcPad p;
    get_pad(ch, &p);
    uint16_t b = p.buttons | 0x0080;
    *hi = (uint32_t)b << 16 | (uint32_t)p.stick_x << 8 | p.stick_y;
    *lo = (uint32_t)p.c_x << 24 | (uint32_t)p.c_y << 16 | (uint32_t)p.trig_l << 8 | p.trig_r;
    /* rumble: the poll command's low bits (0x400300 | motor) */
    input_set_rumble(ch, (int)(g_out[ch] & 3));
}

uint32_t si_read(uint32_t ea, int size) {
    (void)size;
    uint32_t off = ea & 0xFF;
    if (off >= 0x80) {
        uint32_t i = off - 0x80;
        return (uint32_t)g_buf[i] << 24 | g_buf[i + 1] << 16 | g_buf[i + 2] << 8 | g_buf[i + 3];
    }
    if (off < 0x30) {
        int ch = (int)(off / 0xC), r = (int)(off % 0xC);
        if (r == 0) return g_out[ch];
        if (!connected(ch)) return r == 4 ? 0x80000000u : 0;  /* ERRSTAT: no controller */
        if (r == 4) {
            uint32_t hi, lo;
            poll_response(ch, &hi, &lo);
            g_inl_latched[ch] = lo;
            g_status &= ~(0x20000000u >> (8 * ch));            /* RDST cleared by reading INBUFH */
            return hi;
        }
        return g_inl_latched[ch];
    }
    switch (off) {
    case 0x30: return g_poll;
    case 0x34: return g_comcsr;
    case 0x38: {
        /* polled channels always have fresh data; empty ports report no response */
        uint32_t s = g_status;
        for (int ch = 0; ch < 4; ch++) {
            if (connected(ch) && (g_poll >> (7 - ch)) & 1) s |= 0x20000000u >> (8 * ch);
            if (!connected(ch)) s |= 0x08000000u >> (8 * ch);
        }
        return s;
    }
    case 0x3C: return g_exilk;
    default: return 0;
    }
}

void si_write(uint32_t ea, uint32_t v, int size) {
    (void)size;
    uint32_t off = ea & 0xFF;
    if (off >= 0x80) {
        uint32_t i = off - 0x80;
        g_buf[i] = (uint8_t)(v >> 24); g_buf[i + 1] = (uint8_t)(v >> 16);
        g_buf[i + 2] = (uint8_t)(v >> 8); g_buf[i + 3] = (uint8_t)v;
        return;
    }
    if (off < 0x30) {
        if (off % 0xC == 0) g_out[off / 0xC] = v;
        return;
    }
    switch (off) {
    case 0x30: g_poll = v; break;
    case 0x34: {
        /* interrupt status bits are cleared by writing 1; the rest is stored */
        uint32_t ints = COMCSR_TCINT | COMCSR_RDSTINT;
        g_comcsr = (g_comcsr & ints & ~v) | (v & ~ints);
        if (v & 1) transfer();
        break;
    }
    case 0x38:
        g_status &= ~(v & 0x0F0F0F0Fu);  /* error bits clear on write; bit 31 (WR) copies OUTBUFs: nothing to do */
        break;
    case 0x3C: g_exilk = v; break;
    default: break;
    }
}
