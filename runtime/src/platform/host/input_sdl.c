/* Host input: SDL keyboard and gamepads -> GameCube controllers 1-4 (keyboard + first gamepad =
 * port 1, further gamepads = ports 2-4; ports 2-4 exist only while their gamepad is attached). Pumped on the render thread (which
 * owns SDL's event loop); read by the SI model on guest threads.
 *
 * Keyboard: arrows = main stick, I/J/K/L = C stick, T/F/G/H = D-pad, X = A, Z = B, S = X, A = Y,
 *           Enter = START, Q = L, W = R, E = Z.
 * Gamepad (SDL layout): A/B/X/Y, Start, left stick, right stick = C, triggers = L/R (analog, click calibrated
 *           per pad: trigger()), RB = Z, D-pad.
 * Testing: SR_INPUT scripts buttons (input_script.c).
 *          SR_PADS=N connects ports 1..N even without gamepads (scripted multiplayer tests). */
#include <SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <math.h>
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

static uint8_t axis8(int v, int range) {  /* SDL axis -> GameCube 0x80 +- range */
    int r = 0x80 + v * range / 32767;
    return (uint8_t)(r < 1 ? 1 : r > 255 ? 255 : r);
}

/* an SDL stick -> GameCube stick (0x80 +- 100), leaving the keyboard's value alone while centred.
 * Round dead zone of 25% of the throw, and the rest rescaled to start from zero: the game's menus
 * move the cursor from about 20 of 100 and repeat fast while it is held, so with the old 12% square
 * dead zone (which jumped straight to 12) a light touch on a loose pad stick raced through menus.
 * A full push still gives the full range. */
#define STICK_DEAD 0.25
static void stick8(int x, int y, uint8_t* ox, uint8_t* oy) {
    double fx = x / 32767.0, fy = -y / 32767.0, m = sqrt(fx * fx + fy * fy);
    if (m <= STICK_DEAD) return;
    double s = (m - STICK_DEAD) / (1.0 - STICK_DEAD);
    if (s > 1.0) s = 1.0;
    s *= 100.0 / m;
    *ox = axis8((int)lround(fx * s * 32767.0 / 100.0), 100);
    *oy = axis8((int)lround(fy * s * 32767.0 / 100.0), 100);
}

static void controls_load(void);

void input_init(void) {
    input_script_init();
    controls_load();
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

/* Analog triggers. A GameCube trigger is analog for most of its travel and clicks (the digital L / R
 * that Sonic Riders' drift reads) only at the bottom. Pads report different maximums (an Xbox Series X
 * pad over Bluetooth on Android did not always reach the old fixed 29000 of 32767, so a full press
 * sometimes gave a weak or no drift). So each pad's trigger peak is learned while playing: the analog
 * value is scaled so the peak is 255, and the click comes at TRIG_CLICK of the peak. Until a peak is
 * seen, TRIG_PEAK0 is assumed (a full press then always clicks; a deeper one only raises the peak). */
#define TRIG_PEAK0 26000
#define TRIG_CLICK 0.85
static int g_trig_peak[PORTS][2];
static int g_trig_press[PORTS][2];   /* the deepest point of the press going on (0: released) */
static int g_trig_logged[PORTS][2];  /* the deepest press logged so far */

/* the button map: a key and a controller button for each GameCube input. Defaults below; saves/controls.ini
 * (written by the PC launcher's Controls page) changes them: "key.A=J" (SDL key names), "pad.A=a" (SDL controller
 * button names, plus lefttrigger / righttrigger, or none). Controller sticks stay the sticks. */
enum { IN_A, IN_B, IN_X, IN_Y, IN_Z, IN_L, IN_R, IN_START, IN_DUP, IN_DDOWN, IN_DLEFT, IN_DRIGHT, IN_PADS,
       IN_SUP = IN_PADS, IN_SDOWN, IN_SLEFT, IN_SRIGHT, IN_CUP, IN_CDOWN, IN_CLEFT, IN_CRIGHT, IN_COUNT };
static const char* const k_in_name[IN_COUNT] = {
    "A", "B", "X", "Y", "Z", "L", "R", "START", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "STICK_UP", "STICK_DOWN", "STICK_LEFT", "STICK_RIGHT", "C_UP", "C_DOWN", "C_LEFT", "C_RIGHT"};
static const uint16_t k_in_mask[IN_PADS] = {B_A, B_B, B_X, B_Y, B_Z, B_L, B_R, B_START, B_UP, B_DOWN, B_LEFT, B_RIGHT};
static SDL_Scancode g_key[IN_COUNT] = {
    SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_I, SDL_SCANCODE_P, SDL_SCANCODE_U, SDL_SCANCODE_O,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_T, SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_H,
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
enum { PAD_NONE = -1, PAD_LT = 100, PAD_RT = 101 };  /* besides SDL_GameControllerButton values */
static int g_padsrc[IN_PADS] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_LT, PAD_RT, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT};

static void controls_load(void) {
    char file[512];
    snprintf(file, sizeof file, "%s", platform_settings_path());
    char* slash = strrchr(file, '/');
    snprintf(slash ? slash + 1 : file, sizeof file - (size_t)(slash ? slash + 1 - file : 0), "controls.ini");
    FILE* f = fopen(file, "r");
    if (!f) return;
    char line[160];
    int changed = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        int pad = !strncmp(line, "pad.", 4);
        if (!eq || line[0] == '#' || (!pad && strncmp(line, "key.", 4))) continue;
        *eq = 0;
        const char* v = eq + 1;
        for (int i = 0; i < IN_COUNT; i++) {
            if (strcmp(line + 4, k_in_name[i])) continue;
            if (!pad) {
                SDL_Scancode sc = SDL_GetScancodeFromName(v);
                if (sc != SDL_SCANCODE_UNKNOWN || !strcmp(v, "none")) { g_key[i] = sc; changed++; }
                else rt_log("[input] controls.ini: unknown key \"%s\" for %s", v, k_in_name[i]);
            } else if (i < IN_PADS) {
                int b = !strcmp(v, "none") ? PAD_NONE : !strcmp(v, "lefttrigger") ? PAD_LT
                      : !strcmp(v, "righttrigger") ? PAD_RT : (int)SDL_GameControllerGetButtonFromString(v);
                if (b != SDL_CONTROLLER_BUTTON_INVALID) { g_padsrc[i] = b; changed++; }
                else rt_log("[input] controls.ini: unknown controller button \"%s\" for %s", v, k_in_name[i]);
            }
        }
    }
    fclose(f);
    rt_log("[input] %s: %d entries", file, changed);
}

/* for the F1 menu's Controls tab: the map as it is now, with Xbox button names */
int input_map_row(int i, const char** gc, const char** key, const char** pad) {
    static const char* const k_gc[IN_COUNT] = {
        "A", "B", "X", "Y", "Z", "L", "R", "START", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
        "Stick up", "Stick down", "Stick left", "Stick right", "C-stick up", "C-stick down", "C-stick left",
        "C-stick right"};
    static const char* const k_xbox[SDL_CONTROLLER_BUTTON_MAX] = {
        "A", "B", "X", "Y", "View", "Guide", "Menu", "Left stick click", "Right stick click", "LB", "RB",
        "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Share", "Paddle 1", "Paddle 2", "Paddle 3",
        "Paddle 4", "Touchpad"};
    if (i < 0 || i >= IN_COUNT) return 0;
    *gc = k_gc[i];
    *key = g_key[i] == SDL_SCANCODE_UNKNOWN ? "-" : SDL_GetScancodeName(g_key[i]);
    if (i >= IN_PADS) *pad = i < IN_CUP ? "Left stick" : "Right stick";
    else {
        int s = g_padsrc[i];
        *pad = s == PAD_NONE ? "-" : s == PAD_LT ? "LT" : s == PAD_RT ? "RT"
             : s >= 0 && s < SDL_CONTROLLER_BUTTON_MAX && k_xbox[s] ? k_xbox[s] : "?";
    }
    return 1;
}

static int key_down(const Uint8* k, int in) { return g_key[in] != SDL_SCANCODE_UNKNOWN && k[g_key[in]]; }

static void trigger(int port, int side, int v, uint8_t* analog, uint16_t* buttons, uint16_t click) {
    int* peak = &g_trig_peak[port][side];
    int* press = &g_trig_press[port][side];
    if (*peak < TRIG_PEAK0) *peak = TRIG_PEAK0;
    if (v > *peak) *peak = v;
    if (v > 1000 && v > *press) *press = v;
    if (v <= 1000 && *press) {  /* released: a press deeper than any before goes to the log */
        if (*press > g_trig_logged[port][side]) {
            rt_log("[input] port %d %s trigger: pressed to %d of 32767 (peak %d, click from %d)", port + 1,
                   side ? "right" : "left", *press, *peak, (int)(*peak * TRIG_CLICK));
            g_trig_logged[port][side] = *press;
        }
        *press = 0;
    }
    if (v <= 1000) return;
    int a = (int)((long)v * 255 / *peak);
    *analog = (uint8_t)(a > 255 ? 255 : a);
    if (v >= (int)(*peak * TRIG_CLICK)) {
        *buttons |= click;
        *analog = 255;  /* as on a GameCube, a click is the bottom of the travel */
    }
}

/* one gamepad's state added into *p */
/* a GameCube input's mapped controller button held (a trigger half way down counts); for the settings menu, so
 * the phones' on-screen buttons merged into the pad do not switch its tabs */
static int pad_mapped_down(SDL_GameController* c, int in) {
    int src = g_padsrc[in];
    if (src == PAD_NONE) return 0;
    if (src == PAD_LT || src == PAD_RT)
        return SDL_GameControllerGetAxis(c, src == PAD_RT ? SDL_CONTROLLER_AXIS_TRIGGERRIGHT : SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
    return SDL_GameControllerGetButton(c, (SDL_GameControllerButton)src);
}

static void read_gamepad(SDL_GameController* c, int port, GcPad* p) {
    int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    int rx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX), ry = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
    stick8(lx, ly, &p->stick_x, &p->stick_y);
    stick8(rx, ry, &p->c_x, &p->c_y);
    int tv[2] = {SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT),
                 SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)};
    for (int i = 0; i < IN_PADS; i++) {  /* the button map (controls_load) */
        int src = g_padsrc[i];
        if (src == PAD_NONE) continue;
        uint8_t* analog = i == IN_L ? &p->trig_l : i == IN_R ? &p->trig_r : NULL;
        if (src == PAD_LT || src == PAD_RT) {
            int side = src == PAD_RT;
            if (analog) trigger(port, side, tv[side], analog, &p->buttons, k_in_mask[i]);
            else if (tv[side] > 16000) p->buttons |= k_in_mask[i];
        } else if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)src)) {
            p->buttons |= k_in_mask[i];
            if (analog) *analog = 255;
        }
    }
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
        if ((g_ctl[free_port] = SDL_GameControllerOpen(j))) {
            rt_log("[input] port %d: gamepad %s", free_port + 1, SDL_GameControllerName(g_ctl[free_port]));
            memset(g_trig_peak[free_port], 0, sizeof g_trig_peak[free_port]);  /* a new pad learns its own peaks */
            memset(g_trig_press[free_port], 0, sizeof g_trig_press[free_port]);
            memset(g_trig_logged[free_port], 0, sizeof g_trig_logged[free_port]);
        }
    }
}

/* called on the render thread after SDL events were pumped */
void input_pump(void) {
    GcPad p = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    const Uint8* k = SDL_GetKeyboardState(NULL);
#ifdef SR_IOS
    /* iPadOS also sends a controller's buttons to the app as UIKit presses, and SDL turns those into keys (A/Cross =
     * Select = Enter, B = Menu = Escape, D-pad = arrows). Enter is START here, so A paused the race. While a
     * controller is open, the keyboard is ignored. */
    update_gamepads();
    for (int i = 0; i < PORTS; i++)
        if (g_ctl[i]) k = NULL;
#endif
    if (k) {
        /* keyboard (default WASD stick, arrows C-stick, J/K/L/I = A/B/X/Y, U/O/P = L/R/Z, Enter START, T/G/F/H the
         * D-pad; controls_load) */
        int sx = key_down(k, IN_SRIGHT) - key_down(k, IN_SLEFT), sy = key_down(k, IN_SUP) - key_down(k, IN_SDOWN);
        int cx = key_down(k, IN_CRIGHT) - key_down(k, IN_CLEFT), cy = key_down(k, IN_CUP) - key_down(k, IN_CDOWN);
        p.stick_x = (uint8_t)(0x80 + sx * 100); p.stick_y = (uint8_t)(0x80 + sy * 100);
        p.c_x = (uint8_t)(0x80 + cx * 100); p.c_y = (uint8_t)(0x80 + cy * 100);
        for (int i = 0; i < IN_PADS; i++)
            if (key_down(k, i)) p.buttons |= k_in_mask[i];
        if (key_down(k, IN_L)) p.trig_l = 0xFF;
        if (key_down(k, IN_R)) p.trig_r = 0xFF;
    }
    update_gamepads();
    GcPad pads[PORTS];
#if defined(__ANDROID__) || defined(SR_IOS)
    { void touch_apply(GcPad* p); touch_apply(&p); }  /* the touch controls (platform/android/touch_android.c) */
#endif
    pads[0] = p;
    for (int i = 1; i < PORTS; i++) pads[i] = k_neutral;
    for (int i = 0; i < PORTS; i++)
        if (g_ctl[i]) read_gamepad(g_ctl[i], i, &pads[i]);
    /* the settings menu (overlay.h): F1 or the controller's Back held opens it */
    unsigned ov = 0;
    if (k) {
        if (k[SDL_SCANCODE_F1]) ov |= OV_MENU;
        if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) ov |= OV_UP;
        if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) ov |= OV_DOWN;
        if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) ov |= OV_LEFT;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) ov |= OV_RIGHT;
        if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_J]) ov |= OV_ACCEPT;
        if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_K]) ov |= OV_BACK;
        if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_PAGEUP]) ov |= OV_TAB_L;
        if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_PAGEDOWN]) ov |= OV_TAB_R;
        if (k[SDL_SCANCODE_I]) ov |= OV_INFO;
    }
    if (g_ctl[0]) {
        static const struct { SDL_GameControllerButton b; unsigned m; } om[] = {
            {SDL_CONTROLLER_BUTTON_BACK, OV_MENU}, {SDL_CONTROLLER_BUTTON_DPAD_UP, OV_UP},
            {SDL_CONTROLLER_BUTTON_DPAD_DOWN, OV_DOWN}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT, OV_LEFT},
            {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, OV_RIGHT}, {SDL_CONTROLLER_BUTTON_A, OV_ACCEPT},
            {SDL_CONTROLLER_BUTTON_B, OV_BACK}, {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, OV_TAB_L},
            {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, OV_TAB_R}, {SDL_CONTROLLER_BUTTON_Y, OV_INFO}};
        for (size_t i = 0; i < sizeof om / sizeof om[0]; i++)
            if (SDL_GameControllerGetButton(g_ctl[0], om[i].b)) ov |= om[i].m;
    }
    /* the game's own L / R as the player mapped them (a controller's triggers, the keyboard's U / O by default)
     * switch tabs too, like L / R / ZL / ZR on the Switch */
    if ((g_ctl[0] && pad_mapped_down(g_ctl[0], IN_L)) || (k && key_down(k, IN_L))) ov |= OV_TAB_L;
    if ((g_ctl[0] && pad_mapped_down(g_ctl[0], IN_R)) || (k && key_down(k, IN_R))) ov |= OV_TAB_R;
    /* the first input changes go to the log (bug reports: does a controller/F1 reach the game?) */
    static int logged;
    static uint16_t prev_btn[PORTS];
    static unsigned prev_ov;
    for (int i = 0; i < PORTS && logged < 40; i++)
        if (g_ctl[i] && pads[i].buttons != prev_btn[i]) {
            rt_log("[input] port %d buttons %04X stick %02X %02X", i + 1, pads[i].buttons, pads[i].stick_x, pads[i].stick_y);
            prev_btn[i] = pads[i].buttons;
            logged++;
        }
    if ((ov & OV_MENU) != (prev_ov & OV_MENU) && logged < 40) {
        rt_log("[input] menu button (F1 / Back) %s", ov & OV_MENU ? "down" : "up");
        logged++;
    }
    prev_ov = ov;
    ov |= input_script_menu();  /* SR_INPUT "M." steps (tests) */
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
    unpack(atomic_load(&g_pad[chan]), out);
    input_script_apply(chan, out);
}

void input_set_rumble(int chan, int motor) {
    g_rumble[chan & 3] = motor == 1;
}
