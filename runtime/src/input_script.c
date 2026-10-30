/* Scripted input for tests, shared by the PC and Switch input code (on the Switch, set in env.txt).
 *
 * SR_INPUT="900:START,910:-START,1500:A" presses (or releases, "-") buttons at the given VI field
 * counts (with SR_DETCLOCK, game frames, so a script gives the same input on the same frame in every
 * run and on every machine); names: A B X Y START L R Z UP DOWN LEFT RIGHT, prefixed "2." .. "4." for
 * ports 2-4 ("1500:2.START"); "SX=<n>" / "SY=<n>" set the main stick's offset from centre. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

enum { B_LEFT = 0x1, B_RIGHT = 0x2, B_DOWN = 0x4, B_UP = 0x8, B_Z = 0x10, B_R = 0x20, B_L = 0x40,
       B_A = 0x100, B_B = 0x200, B_X = 0x400, B_Y = 0x800, B_START = 0x1000 };

#define PORTS 4
typedef struct { uint32_t field; uint16_t mask; int press, port, axis, value; } Step;  /* axis 1/2: SX=/SY= */
static Step g_script[1024];
static int g_nscript;
static uint16_t g_script_held[PORTS];
static int g_script_stick[PORTS][2];

unsigned long long gx_frames_presented(void);
int ppc_detclock(void);
uint32_t ppc_detclock_steps(void);

static uint16_t button_mask(const char* n) {
    static const struct { const char* name; uint16_t m; } t[] = {
        {"A", B_A}, {"B", B_B}, {"X", B_X}, {"Y", B_Y}, {"START", B_START}, {"L", B_L}, {"R", B_R}, {"Z", B_Z},
        {"UP", B_UP}, {"DOWN", B_DOWN}, {"LEFT", B_LEFT}, {"RIGHT", B_RIGHT}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(n, t[i].name)) return t[i].m;
    return 0;
}

void input_script_init(void) {
    const char* s = getenv("SR_INPUT");
    if (!s) return;
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
        int axis = (name[0] == 'S' && (name[1] == 'X' || name[1] == 'Y') && name[2] == '=') ? 1 + (name[1] == 'Y') : 0;
        g_script[g_nscript++] = (Step){(uint32_t)atoi(tok), axis ? 0 : button_mask(name), press, port, axis, axis ? atoi(name + 3) : 0};
    }
    rt_log("[input] scripted input: %d steps", g_nscript);
}

/* adds the scripted buttons/stick for that port to *out (called where the game reads a pad) */
void input_script_apply(int chan, GcPad* out) {
    if (!g_nscript) return;
    chan &= 3;
    uint32_t f = ppc_detclock() ? ppc_detclock_steps() : vi_field_count();
    static int next;
    while (next < g_nscript && g_script[next].field <= f) {
        uint16_t* held = &g_script_held[g_script[next].port];
        rt_log("[input] script step at field %u = presented frame %llu", g_script[next].field, gx_frames_presented());
        if (g_script[next].axis) g_script_stick[g_script[next].port][g_script[next].axis - 1] = g_script[next].value;
        else if (g_script[next].press) *held |= g_script[next].mask;
        else *held &= (uint16_t)~g_script[next].mask;
        next++;
    }
    out->buttons |= g_script_held[chan];
    if (g_script_stick[chan][0] || g_script_stick[chan][1]) {
        out->stick_x = (uint8_t)(0x80 + g_script_stick[chan][0]);
        out->stick_y = (uint8_t)(0x80 + g_script_stick[chan][1]);
    }
}
