/* Switch input: up to four controllers -> GameCube ports 1-4. Pumped on the render thread.
 *
 * Player 1 is the handheld console or the first controller; players 2-4 are the system's player
 * slots 2-4. Port 1 always reports a controller; ports 2-4 report one only while that player's
 * controller is connected (the game's PAD library sees it plugged in or out).
 *
 * Pro Controller / Joy-Con pair / handheld: A B X Y = A B X Y, + = START, ZL / ZR = L / R (full
 * press), R = Z, D-pad = D-pad, left stick = main stick, right stick = C stick.
 * Single Joy-Con held sideways: the four face buttons by position (right = A, bottom = B, left = Y,
 * top = X), + or - = START, SL / SR = L / R, stick click = Z; the stick is rotated to match. The
 * system does not rotate a sideways Joy-Con itself (same mapping as RetroArch's Switch driver).
 *
 * Minus held on player 1's controller (on a single Joy-Con: its + or -, which is also START) opens
 * the settings menu; its "Controllers" item shows the system's controller screen (pair / split Joy-Cons,
 * change player order). */
#include <stdatomic.h>
#include <stdlib.h>
#include <switch.h>

#include "overlay.h"
#include "runtime.h"

enum { B_LEFT = 0x1, B_RIGHT = 0x2, B_DOWN = 0x4, B_UP = 0x8, B_Z = 0x10, B_R = 0x20, B_L = 0x40,
       B_A = 0x100, B_B = 0x200, B_X = 0x400, B_Y = 0x800, B_START = 0x1000 };

#define PLAYERS 4

static PadState g_pad[PLAYERS];
static PadState g_handheld;  /* the console's attached Joy-Cons: player 1 when No1 has no controller */
static _Atomic uint64_t g_state[PLAYERS];
static _Atomic int g_connected[PLAYERS];
static int g_logged[PLAYERS];  /* last style set logged per player */

static uint8_t axis8(int v) {
    int r = 0x80 + v * 100 / 32767;
    return (uint8_t)(r < 1 ? 1 : r > 255 ? 255 : r);
}

static uint64_t pack(const GcPad* p) {
    return (uint64_t)p->buttons | (uint64_t)p->stick_x << 16 | (uint64_t)p->stick_y << 24 | (uint64_t)p->c_x << 32 |
           (uint64_t)p->c_y << 40 | (uint64_t)p->trig_l << 48 | (uint64_t)p->trig_r << 56;
}

static const GcPad k_neutral = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};

void input_init(void) {
    padConfigureInput(PLAYERS, HidNpadStyleSet_NpadStandard);
    /* single Joy-Cons are held sideways (only matters once the user splits a pair) */
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
    /* player 1's controller and handheld mode are read separately: one combined PadState reported
     * a sideways single Joy-Con on No1 as "handheld", and it got the two-handed mapping */
    padInitialize(&g_pad[0], HidNpadIdType_No1);
    padInitialize(&g_handheld, HidNpadIdType_Handheld);
    padInitialize(&g_pad[1], HidNpadIdType_No2);
    padInitialize(&g_pad[2], HidNpadIdType_No3);
    padInitialize(&g_pad[3], HidNpadIdType_No4);
    for (int i = 0; i < PLAYERS; i++) atomic_store(&g_state[i], pack(&k_neutral));
    atomic_store(&g_connected[0], 1);
}

/* the system's controller screen: pair controllers, split Joy-Cons, set the player order. Blocks
 * the render thread (and so the game) until it closes. */
static void show_controller_applet(void) {
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = 0;
    arg.hdr.player_count_max = PLAYERS;
    arg.hdr.enable_take_over_connection = 1;
    arg.hdr.enable_left_justify = 1;
    arg.hdr.enable_permit_joy_dual = 1;
    arg.hdr.enable_single_mode = 0;
    HidLaControllerSupportResultInfo info;
    Result rc = hidLaShowControllerSupportForSystem(&info, &arg, false);
    if (R_FAILED(rc)) rc = hidLaShowControllerSupport(&info, &arg);
    if (R_FAILED(rc)) rt_log("[input] controller screen failed (0x%X)", rc);
    else rt_log("[input] controller screen closed: %d players", (int)info.player_count);
}

/* one player's buttons/sticks; *ov gets the menu buttons (player 1 only) */
static GcPad read_player(int i, unsigned* ov) {
    PadState* pad = &g_pad[i];
    int handheld = 0;
    if (i == 0 && !padIsConnected(pad) && padIsConnected(&g_handheld)) {
        pad = &g_handheld;
        handheld = 1;
    }
    u64 b = padGetButtons(pad);
    u32 style = padGetStyleSet(pad);
    u32 attr = padGetAttributes(pad);
    GcPad p = k_neutral;
    unsigned o = 0;
    /* a lone Joy-Con: assigned singly (JoyLeft/JoyRight style), or still in dual mode with only one
     * half connected; either way it is held sideways */
    int single_left = (style & HidNpadStyleTag_NpadJoyLeft) && !(style & HidNpadStyleTag_NpadJoyDual);
    int single_right = (style & HidNpadStyleTag_NpadJoyRight) && !(style & HidNpadStyleTag_NpadJoyDual);
    if (style & HidNpadStyleTag_NpadJoyDual) {
        int l = (attr & HidNpadAttribute_IsLeftConnected) != 0, r = (attr & HidNpadAttribute_IsRightConnected) != 0;
        if (l && !r) single_left = 1;
        if (r && !l) single_right = 1;
    }
    if (!handheld && !(style & HidNpadStyleTag_NpadFullKey) && (single_left || single_right)) {
        /* a single Joy-Con held sideways */
        int left = single_left;
        HidAnalogStickState s = padGetStickPos(pad, left ? 0 : 1);
        int x = left ? -s.y : s.y, y = left ? s.x : -s.x;
        p.stick_x = axis8(x); p.stick_y = axis8(y);
        if (left) {
            if (b & HidNpadButton_Down) p.buttons |= B_A;
            if (b & HidNpadButton_Left) p.buttons |= B_B;
            if (b & HidNpadButton_Right) p.buttons |= B_X;
            if (b & HidNpadButton_Up) p.buttons |= B_Y;
            if (b & HidNpadButton_Minus) { p.buttons |= B_START; o |= OV_MENU; }
            if (b & HidNpadButton_StickL) p.buttons |= B_Z;
            if (b & HidNpadButton_LeftSL) { p.buttons |= B_L; p.trig_l = 0xFF; }
            if (b & HidNpadButton_LeftSR) { p.buttons |= B_R; p.trig_r = 0xFF; }
        } else {
            if (b & HidNpadButton_X) p.buttons |= B_A;
            if (b & HidNpadButton_A) p.buttons |= B_B;
            if (b & HidNpadButton_Y) p.buttons |= B_X;
            if (b & HidNpadButton_B) p.buttons |= B_Y;
            if (b & HidNpadButton_Plus) { p.buttons |= B_START; o |= OV_MENU; }
            if (b & HidNpadButton_StickR) p.buttons |= B_Z;
            if (b & HidNpadButton_RightSL) { p.buttons |= B_L; p.trig_l = 0xFF; }
            if (b & HidNpadButton_RightSR) { p.buttons |= B_R; p.trig_r = 0xFF; }
        }
        /* menu navigation with the stick and the face buttons */
        if (y > 16000) o |= OV_UP;
        if (y < -16000) o |= OV_DOWN;
        if (x < -16000) o |= OV_LEFT;
        if (x > 16000) o |= OV_RIGHT;
        if (p.buttons & B_A) o |= OV_ACCEPT;
        if (p.buttons & B_B) o |= OV_BACK;
    } else {
        HidAnalogStickState l = padGetStickPos(pad, 0), r = padGetStickPos(pad, 1);
        p.stick_x = axis8(l.x); p.stick_y = axis8(l.y); p.c_x = axis8(r.x); p.c_y = axis8(r.y);
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
        if (b & HidNpadButton_Minus) o |= OV_MENU;
        if (b & HidNpadButton_Up) o |= OV_UP;
        if (b & HidNpadButton_Down) o |= OV_DOWN;
        if (b & HidNpadButton_Left) o |= OV_LEFT;
        if (b & HidNpadButton_Right) o |= OV_RIGHT;
        if (b & HidNpadButton_A) o |= OV_ACCEPT;
        if (b & HidNpadButton_B) o |= OV_BACK;
    }
    int tag = (int)(style | attr << 16);
    if (tag != g_logged[i]) {
        g_logged[i] = tag;
        if (style)
            rt_log("[input] player %d: controller style %X attributes %X%s%s", i + 1, style, attr, handheld ? " (handheld)" : "",
                   !handheld && !(style & HidNpadStyleTag_NpadFullKey) && (single_left || single_right) ? " (single Joy-Con, sideways)" : "");
        else rt_log("[input] player %d: disconnected", i + 1);
    }
    *ov = o;
    return p;
}

void input_pump(void) {
    if (overlay_take_controllers_request()) show_controller_applet();
    GcPad p[PLAYERS];
    unsigned ov = 0;
    for (int i = 0; i < PLAYERS; i++) {
        padUpdate(&g_pad[i]);
        if (i == 0) padUpdate(&g_handheld);
        unsigned o;
        p[i] = read_player(i, &o);
        if (i == 0) ov = o;
        if (i > 0) atomic_store(&g_connected[i], padIsConnected(&g_pad[i]) ? 1 : 0);
    }
    /* after the console wakes (or the game gets focus back): the first button press goes to the log,
     * which tells a dead controller apart from a frozen picture (platform_switch.c sets the flag) */
    extern _Atomic int g_switch_log_input;
    if (atomic_load(&g_switch_log_input))
        for (int i = 0; i < PLAYERS; i++)
            if (p[i].buttons) {
                rt_log("[input] first buttons after wake/focus: player %d %04X", i + 1, p[i].buttons);
                atomic_store(&g_switch_log_input, 0);
                break;
            }
    /* the settings menu (overlay.h) is driven by player 1; while it is open every port is neutral */
    int menu = overlay_input(ov);
    for (int i = 0; i < PLAYERS; i++) atomic_store(&g_state[i], pack(menu ? &k_neutral : &p[i]));
}

int input_connected(int chan) {
    return chan >= 0 && chan < PLAYERS && atomic_load(&g_connected[chan]);
}

void input_get_pad(int chan, GcPad* out) {
    uint64_t v = atomic_load(&g_state[chan & 3]);
    out->buttons = (uint16_t)v; out->stick_x = (uint8_t)(v >> 16); out->stick_y = (uint8_t)(v >> 24);
    out->c_x = (uint8_t)(v >> 32); out->c_y = (uint8_t)(v >> 40); out->trig_l = (uint8_t)(v >> 48);
    out->trig_r = (uint8_t)(v >> 56);
}

void input_set_rumble(int chan, int motor) { (void)chan; (void)motor; }
