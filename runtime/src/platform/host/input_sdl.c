/* Host input: SDL keyboard and gamepads -> GameCube controllers 1-4 (keyboard + first gamepad =
 * port 1, further gamepads = ports 2-4; ports 2-4 exist only while their gamepad is attached). Pumped on the render thread (which
 * owns SDL's event loop); read by the SI model on guest threads.
 *
 * Keyboard: arrows = main stick, I/J/K/L = C stick, T/F/G/H = D-pad, X = A, Z = B, S = X, A = Y,
 *           Enter = START, Q = L, W = R, E = Z.
 * Gamepad (SDL layout): A/B/X/Y, Start, left stick, right stick = C, triggers = L/R, RB = Z, D-pad.
 * Testing: SR_INPUT="900:START,910:-START,1500:A" presses (or releases, "-") buttons at the given
 *          VI field counts; names: A B X Y START L R Z UP DOWN LEFT RIGHT, prefixed "2." .. "4." for
 *          ports 2-4 ("1500:2.START").
 *          SR_PADS=N connects ports 1..N even without gamepads (scripted multiplayer tests). */
#include <SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overlay.h"
#include "runtime.h"

enum { B_LEFT = 0x1, B_RIGHT = 0x2, B_DOWN = 0x4, B_UP = 0x8, B_Z = 0x10, B_R = 0x20, B_L = 0x40,
       B_A = 0x100, B_B = 0x200, B_X = 0x400, B_Y = 0x800, B_START = 0x1000 };

#define PORTS 4
static _Atomic uint64_t g_pad[PORTS];  /* packed GcPad per port */
static _Atomic int g_attached[PORTS];
static SDL_GameController* g_ctl[PORTS];
static volatile int g_rumble[PORTS];
static int g_forced_ports = 1;  /* SR_PADS */

static uint64_t pack(const GcPad* p) {
    return (uint64_t)p->buttons | (uint64_t)p->stick_x << 16 | (uint64_t)p->stick_y << 24 | (uint64_t)p->c_x << 32 |
           (uint64_t)p->c_y << 40 | (uint64_t)p->trig_l << 48 | (uint64_t)p->trig_r << 56;
}
static void unpack(uint64_t v, GcPad* p) {
    p->buttons = (uint16_t)v; p->stick_x = (uint8_t)(v >> 16); p->stick_y = (uint8_t)(v >> 24);
    p->c_x = (uint8_t)(v >> 32); p->c_y = (uint8_t)(v >> 40); p->trig_l = (uint8_t)(v >> 48); p->trig_r = (uint8_t)(v >> 56);
}

/* ---- scripted input ---- */
typedef struct { uint32_t field; uint16_t mask; int press, port; } Step;
static Step g_script[1024];
static int g_nscript;
static uint16_t g_script_held[PORTS];

static uint16_t button_mask(const char* n) {
    static const struct { const char* name; uint16_t m; } t[] = {
        {"A", B_A}, {"B", B_B}, {"X", B_X}, {"Y", B_Y}, {"START", B_START}, {"L", B_L}, {"R", B_R}, {"Z", B_Z},
        {"UP", B_UP}, {"DOWN", B_DOWN}, {"LEFT", B_LEFT}, {"RIGHT", B_RIGHT}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(n, t[i].name)) return t[i].m;
    return 0;
}

static void parse_script(const char* s) {
    static char buf[16384];
    snprintf(buf, sizeof buf, "%s", s);
    for (char* tok = strtok(buf, ","); tok && g_nscript < 1024; tok = strtok(NULL, ",")) {
        char* colon = strchr(tok, ':');
        if (!colon) continue;
        *colon = 0;
        const char* name = colon + 1;
        int press = 1, port = 0;
        if (*name == '-') { press = 0; name++; }
        if (name[0] >= '1' && name[0] <= '4' && name[1] == '.') { port = name[0] - '1'; name += 2; }
        g_script[g_nscript++] = (Step){(uint32_t)atoi(tok), button_mask(name), press, port};
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
    const char* n = getenv("SR_PADS");
    if (n) {
        g_forced_ports = atoi(n);
        if (g_forced_ports < 1) g_forced_ports = 1;
        if (g_forced_ports > PORTS) g_forced_ports = PORTS;
        rt_log("[input] SR_PADS: ports 1-%d connected", g_forced_ports);
    }
    GcPad p = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    for (int i = 0; i < PORTS; i++) atomic_store(&g_pad[i], pack(&p));
}

static const GcPad k_neutral = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};

/* one gamepad's state added into *p */
static void read_gamepad(SDL_GameController* c, int port, GcPad* p) {
    int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    int rx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX), ry = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
    if (abs(lx) > 4000 || abs(ly) > 4000) { p->stick_x = axis8(lx, 100); p->stick_y = axis8(-ly, 100); }
    if (abs(rx) > 4000 || abs(ry) > 4000) { p->c_x = axis8(rx, 100); p->c_y = axis8(-ry, 100); }
    int tl = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT), tr = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (tl > 1000) { p->trig_l = (uint8_t)(tl * 255 / 32767); if (tl > 29000) p->buttons |= B_L; }
    if (tr > 1000) { p->trig_r = (uint8_t)(tr * 255 / 32767); if (tr > 29000) p->buttons |= B_R; }
    static const struct { SDL_GameControllerButton b; uint16_t m; } map[] = {
        {SDL_CONTROLLER_BUTTON_A, B_A}, {SDL_CONTROLLER_BUTTON_B, B_B}, {SDL_CONTROLLER_BUTTON_X, B_X},
        {SDL_CONTROLLER_BUTTON_Y, B_Y}, {SDL_CONTROLLER_BUTTON_START, B_START},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, B_Z}, {SDL_CONTROLLER_BUTTON_DPAD_UP, B_UP},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, B_DOWN}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT, B_LEFT},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, B_RIGHT}};
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (SDL_GameControllerGetButton(c, map[i].b)) p->buttons |= map[i].m;
    static int rumbling[PORTS];
    if (g_rumble[port] != rumbling[port]) {
        rumbling[port] = g_rumble[port];
        int r = rumbling[port];
        SDL_GameControllerRumble(c, r ? 0x8000 : 0, r ? 0x8000 : 0, r ? 1000 : 0);
    }
}

/* gamepads -> ports in the order they were opened; a detached one frees its port */
static void update_gamepads(void) {
    for (int i = 0; i < PORTS; i++)
        if (g_ctl[i] && !SDL_GameControllerGetAttached(g_ctl[i])) {
            rt_log("[input] port %d: gamepad detached", i + 1);
            SDL_GameControllerClose(g_ctl[i]);
            g_ctl[i] = NULL;
        }
    static uint32_t last_scan;
    uint32_t now = SDL_GetTicks();
    if (now - last_scan < 1000 && last_scan) return;
    last_scan = now;
    for (int j = 0; j < SDL_NumJoysticks(); j++) {
        if (!SDL_IsGameController(j)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(j);
        int used = 0, free_port = -1;
        for (int i = 0; i < PORTS; i++) {
            if (g_ctl[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_ctl[i])) == id) used = 1;
            if (!g_ctl[i] && free_port < 0) free_port = i;
        }
        if (used || free_port < 0) continue;
        if ((g_ctl[free_port] = SDL_GameControllerOpen(j)))
            rt_log("[input] port %d: gamepad %s", free_port + 1, SDL_GameControllerName(g_ctl[free_port]));
    }
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
    update_gamepads();
    GcPad pads[PORTS];
    pads[0] = p;
    for (int i = 1; i < PORTS; i++) pads[i] = k_neutral;
    for (int i = 0; i < PORTS; i++)
        if (g_ctl[i]) read_gamepad(g_ctl[i], i, &pads[i]);
    /* the settings menu (overlay.h): F1 or the controller's Back held opens it */
    unsigned ov = 0;
    if (k) {
        if (k[SDL_SCANCODE_F1]) ov |= OV_MENU;
        if (k[SDL_SCANCODE_UP]) ov |= OV_UP;
        if (k[SDL_SCANCODE_DOWN]) ov |= OV_DOWN;
        if (k[SDL_SCANCODE_LEFT]) ov |= OV_LEFT;
        if (k[SDL_SCANCODE_RIGHT]) ov |= OV_RIGHT;
        if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_X]) ov |= OV_ACCEPT;
        if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_Z]) ov |= OV_BACK;
    }
    if (g_ctl[0]) {
        static const struct { SDL_GameControllerButton b; unsigned m; } om[] = {
            {SDL_CONTROLLER_BUTTON_BACK, OV_MENU}, {SDL_CONTROLLER_BUTTON_DPAD_UP, OV_UP},
            {SDL_CONTROLLER_BUTTON_DPAD_DOWN, OV_DOWN}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT, OV_LEFT},
            {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, OV_RIGHT}, {SDL_CONTROLLER_BUTTON_A, OV_ACCEPT},
            {SDL_CONTROLLER_BUTTON_B, OV_BACK}};
        for (size_t i = 0; i < sizeof om / sizeof om[0]; i++)
            if (SDL_GameControllerGetButton(g_ctl[0], om[i].b)) ov |= om[i].m;
    }
    int menu = overlay_input(ov);
    (void)overlay_take_controllers_request();  /* nothing to set up on the desktop */
    for (int i = 0; i < PORTS; i++) {
        atomic_store(&g_pad[i], pack(menu ? &k_neutral : &pads[i]));
        atomic_store(&g_attached[i], i < g_forced_ports || g_ctl[i] != NULL);
    }
}

int input_connected(int chan) {
    return chan >= 0 && chan < PORTS && atomic_load(&g_attached[chan]);
}

void input_get_pad(int chan, GcPad* out) {
    chan &= 3;
    /* scripted steps are applied in step with the game's own clock (VI fields) */
    if (g_nscript) {
        uint32_t f = vi_field_count();
        static int next;
        while (next < g_nscript && g_script[next].field <= f) {
            uint16_t* held = &g_script_held[g_script[next].port];
            if (g_script[next].press) *held |= g_script[next].mask;
            else *held &= (uint16_t)~g_script[next].mask;
            next++;
        }
    }
    unpack(atomic_load(&g_pad[chan]), out);
    out->buttons |= g_script_held[chan];
}

void input_set_rumble(int chan, int motor) {
    g_rumble[chan & 3] = motor == 1;
}
