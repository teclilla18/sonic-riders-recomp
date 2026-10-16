/* Switch input: Joy-Con / Pro Controller (any player-1 style) -> GameCube controller 1.
 * A B X Y = A B X Y, + = START, ZL / ZR = L / R (full press), R = Z, D-pad = D-pad,
 * left stick = main stick, right stick = C stick. Pumped on the render thread. */
#include <stdatomic.h>
#include <stdlib.h>
#include <switch.h>

#include "runtime.h"

enum { B_LEFT = 0x1, B_RIGHT = 0x2, B_DOWN = 0x4, B_UP = 0x8, B_Z = 0x10, B_R = 0x20, B_L = 0x40,
       B_A = 0x100, B_B = 0x200, B_X = 0x400, B_Y = 0x800, B_START = 0x1000 };

static PadState g_pad;
static _Atomic uint64_t g_state;

static uint8_t axis8(int v) {
    int r = 0x80 + v * 100 / 32767;
    return (uint8_t)(r < 1 ? 1 : r > 255 ? 255 : r);
}

void input_init(void) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    GcPad p = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    uint64_t v = (uint64_t)p.buttons | (uint64_t)p.stick_x << 16 | (uint64_t)p.stick_y << 24 | (uint64_t)p.c_x << 32 |
                 (uint64_t)p.c_y << 40;
    atomic_store(&g_state, v);
}

void input_pump(void) {
    padUpdate(&g_pad);
    u64 b = padGetButtons(&g_pad);
    HidAnalogStickState l = padGetStickPos(&g_pad, 0), r = padGetStickPos(&g_pad, 1);
    GcPad p = {0, axis8(l.x), axis8(l.y), axis8(r.x), axis8(r.y), 0, 0};
    if (b & HidNpadButton_A) p.buttons |= B_A;
    if (b & HidNpadButton_B) p.buttons |= B_B;
    if (b & HidNpadButton_X) p.buttons |= B_X;
    if (b & HidNpadButton_Y) p.buttons |= B_Y;
    if (b & HidNpadButton_Plus) p.buttons |= B_START;
    if (b & HidNpadButton_R) p.buttons |= B_Z;
    if (b & HidNpadButton_ZL) { p.buttons |= B_L; p.trig_l = 0xFF; }
    if (b & HidNpadButton_ZR) { p.buttons |= B_R; p.trig_r = 0xFF; }
    if (b & HidNpadButton_Up) p.buttons |= B_UP;
    if (b & HidNpadButton_Down) p.buttons |= B_DOWN;
    if (b & HidNpadButton_Left) p.buttons |= B_LEFT;
    if (b & HidNpadButton_Right) p.buttons |= B_RIGHT;
    uint64_t v = (uint64_t)p.buttons | (uint64_t)p.stick_x << 16 | (uint64_t)p.stick_y << 24 | (uint64_t)p.c_x << 32 |
                 (uint64_t)p.c_y << 40 | (uint64_t)p.trig_l << 48 | (uint64_t)p.trig_r << 56;
    atomic_store(&g_state, v);
}

void input_get_pad(int chan, GcPad* out) {
    (void)chan;
    uint64_t v = atomic_load(&g_state);
    out->buttons = (uint16_t)v; out->stick_x = (uint8_t)(v >> 16); out->stick_y = (uint8_t)(v >> 24);
    out->c_x = (uint8_t)(v >> 32); out->c_y = (uint8_t)(v >> 40); out->trig_l = (uint8_t)(v >> 48);
    out->trig_r = (uint8_t)(v >> 56);
}

void input_set_rumble(int chan, int motor) { (void)chan; (void)motor; }
