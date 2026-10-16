/* Host input: SDL keyboard and gamepads -> GameCube controller 1. Pumped on the render thread (which
 * owns SDL's event loop); read by the SI model on guest threads.
 *
 * Keyboard: arrows = main stick, I/J/K/L = C stick, T/F/G/H = D-pad, X = A, Z = B, S = X, A = Y,
 *           Enter = START, Q = L, W = R, E = Z.
 * Gamepad (SDL layout): A/B/X/Y, Start, left stick, right stick = C, triggers = L/R, RB = Z, D-pad.
 * Testing: SR_INPUT="900:START,910:-START,1500:A" presses (or releases, "-") buttons at the given
 *          VI field counts; names: A B X Y START L R Z UP DOWN LEFT RIGHT. */
#include <SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

enum { B_LEFT = 0x1, B_RIGHT = 0x2, B_DOWN = 0x4, B_UP = 0x8, B_Z = 0x10, B_R = 0x20, B_L = 0x40,
       B_A = 0x100, B_B = 0x200, B_X = 0x400, B_Y = 0x800, B_START = 0x1000 };

static _Atomic uint64_t g_pad;  /* packed GcPad for controller 1 */
static SDL_GameController* g_ctl;
static volatile int g_rumble;

static uint64_t pack(const GcPad* p) {
    return (uint64_t)p->buttons | (uint64_t)p->stick_x << 16 | (uint64_t)p->stick_y << 24 | (uint64_t)p->c_x << 32 |
           (uint64_t)p->c_y << 40 | (uint64_t)p->trig_l << 48 | (uint64_t)p->trig_r << 56;
}
static void unpack(uint64_t v, GcPad* p) {
    p->buttons = (uint16_t)v; p->stick_x = (uint8_t)(v >> 16); p->stick_y = (uint8_t)(v >> 24);
    p->c_x = (uint8_t)(v >> 32); p->c_y = (uint8_t)(v >> 40); p->trig_l = (uint8_t)(v >> 48); p->trig_r = (uint8_t)(v >> 56);
}

/* ---- scripted input ---- */
typedef struct { uint32_t field; uint16_t mask; int press; } Step;
static Step g_script[256];
static int g_nscript;
static uint16_t g_script_held;

static uint16_t button_mask(const char* n) {
    static const struct { const char* name; uint16_t m; } t[] = {
        {"A", B_A}, {"B", B_B}, {"X", B_X}, {"Y", B_Y}, {"START", B_START}, {"L", B_L}, {"R", B_R}, {"Z", B_Z},
        {"UP", B_UP}, {"DOWN", B_DOWN}, {"LEFT", B_LEFT}, {"RIGHT", B_RIGHT}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(n, t[i].name)) return t[i].m;
    return 0;
}

static void parse_script(const char* s) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", s);
    for (char* tok = strtok(buf, ","); tok && g_nscript < 256; tok = strtok(NULL, ",")) {
        char* colon = strchr(tok, ':');
        if (!colon) continue;
        *colon = 0;
        const char* name = colon + 1;
        int press = 1;
        if (*name == '-') { press = 0; name++; }
        g_script[g_nscript++] = (Step){(uint32_t)atoi(tok), button_mask(name), press};
    }
    rt_log("[input] scripted input: %d steps", g_nscript);
}

static uint8_t axis8(int v, int range) {  /* SDL axis -> GameCube 0x80 +- range */
    int r = 0x80 + v * range / 32767;
    return (uint8_t)(r < 1 ? 1 : r > 255 ? 255 : r);
}

void input_init(void) {
    const char* s = getenv("SR_INPUT");
    if (s) parse_script(s);
    GcPad p = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    atomic_store(&g_pad, pack(&p));
}

/* called on the render thread after SDL events were pumped */
void input_pump(void) {
    GcPad p = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    const Uint8* k = SDL_GetKeyboardState(NULL);
    if (k) {
        int sx = (k[SDL_SCANCODE_RIGHT] ? 1 : 0) - (k[SDL_SCANCODE_LEFT] ? 1 : 0);
        int sy = (k[SDL_SCANCODE_UP] ? 1 : 0) - (k[SDL_SCANCODE_DOWN] ? 1 : 0);
        int cx = (k[SDL_SCANCODE_L] ? 1 : 0) - (k[SDL_SCANCODE_J] ? 1 : 0);
        int cy = (k[SDL_SCANCODE_I] ? 1 : 0) - (k[SDL_SCANCODE_K] ? 1 : 0);
        p.stick_x = (uint8_t)(0x80 + sx * 100); p.stick_y = (uint8_t)(0x80 + sy * 100);
        p.c_x = (uint8_t)(0x80 + cx * 100); p.c_y = (uint8_t)(0x80 + cy * 100);
        if (k[SDL_SCANCODE_X]) p.buttons |= B_A;
        if (k[SDL_SCANCODE_Z]) p.buttons |= B_B;
        if (k[SDL_SCANCODE_S]) p.buttons |= B_X;
        if (k[SDL_SCANCODE_A]) p.buttons |= B_Y;
        if (k[SDL_SCANCODE_RETURN]) p.buttons |= B_START;
        if (k[SDL_SCANCODE_E]) p.buttons |= B_Z;
        if (k[SDL_SCANCODE_Q]) { p.buttons |= B_L; p.trig_l = 0xFF; }
        if (k[SDL_SCANCODE_W]) { p.buttons |= B_R; p.trig_r = 0xFF; }
        if (k[SDL_SCANCODE_T]) p.buttons |= B_UP;
        if (k[SDL_SCANCODE_G]) p.buttons |= B_DOWN;
        if (k[SDL_SCANCODE_F]) p.buttons |= B_LEFT;
        if (k[SDL_SCANCODE_H]) p.buttons |= B_RIGHT;
    }
    if (!g_ctl) {
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i) && (g_ctl = SDL_GameControllerOpen(i))) {
                rt_log("[input] gamepad: %s", SDL_GameControllerName(g_ctl));
                break;
            }
    }
    if (g_ctl && SDL_GameControllerGetAttached(g_ctl)) {
        SDL_GameController* c = g_ctl;
        int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        int rx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX), ry = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
        if (abs(lx) > 4000 || abs(ly) > 4000) { p.stick_x = axis8(lx, 100); p.stick_y = axis8(-ly, 100); }
        if (abs(rx) > 4000 || abs(ry) > 4000) { p.c_x = axis8(rx, 100); p.c_y = axis8(-ry, 100); }
        int tl = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT), tr = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (tl > 1000) { p.trig_l = (uint8_t)(tl * 255 / 32767); if (tl > 29000) p.buttons |= B_L; }
        if (tr > 1000) { p.trig_r = (uint8_t)(tr * 255 / 32767); if (tr > 29000) p.buttons |= B_R; }
        static const struct { SDL_GameControllerButton b; uint16_t m; } map[] = {
            {SDL_CONTROLLER_BUTTON_A, B_A}, {SDL_CONTROLLER_BUTTON_B, B_B}, {SDL_CONTROLLER_BUTTON_X, B_X},
            {SDL_CONTROLLER_BUTTON_Y, B_Y}, {SDL_CONTROLLER_BUTTON_START, B_START},
            {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, B_Z}, {SDL_CONTROLLER_BUTTON_DPAD_UP, B_UP},
            {SDL_CONTROLLER_BUTTON_DPAD_DOWN, B_DOWN}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT, B_LEFT},
            {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, B_RIGHT}};
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
            if (SDL_GameControllerGetButton(c, map[i].b)) p.buttons |= map[i].m;
        static int rumbling;
        if (g_rumble != rumbling) {
            rumbling = g_rumble;
            SDL_GameControllerRumble(c, rumbling ? 0x8000 : 0, rumbling ? 0x8000 : 0, rumbling ? 1000 : 0);
        }
    }
    p.buttons |= g_script_held;
    atomic_store(&g_pad, pack(&p));
}

void input_get_pad(int chan, GcPad* out) {
    (void)chan;
    /* scripted steps are applied in step with the game's own clock (VI fields) */
    if (g_nscript) {
        uint32_t f = vi_field_count();
        static int next;
        while (next < g_nscript && g_script[next].field <= f) {
            if (g_script[next].press) g_script_held |= g_script[next].mask;
            else g_script_held &= (uint16_t)~g_script[next].mask;
            next++;
        }
    }
    unpack(atomic_load(&g_pad), out);
    out->buttons |= g_script_held;
}

void input_set_rumble(int chan, int motor) {
    if (chan == 0) g_rumble = motor == 1;
}
