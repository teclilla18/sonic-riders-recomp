/* Touch controls for phones (docs/ANDROID_PLAN.md section 7), drawn by render_vk.c (vk_touch.frag).
 *
 * Sonic Riders' controls (GameCube): stick = steer / tricks in the air, A = jump (hold to charge),
 * B or X = boost, L or R = drift (held), L+R = tornado (or leave turbulence), START = pause.
 * Layout, in units of the screen height H (so it scales with the phone), mirrored for the thumbs:
 *   left half: a floating stick that appears where the thumb lands (base radius 0.16 H, 12% dead zone),
 *     and whose base follows the thumb when it is dragged far past the edge;
 *   right: JUMP (the largest, where the thumb rests), BOOST to its left, DRIFT above it, TORNADO between;
 *     a thumb can slide from one to another without lifting (the button under it is the one pressed);
 *   top corners: PAUSE (START) on the right, MENU (the settings overlay) on the left, small and out of the
 *     way; both clear of the camera cutout (side margin 0.09 H).
 * While the menu is open, fingers act on it (overlay_pointer). Using a gamepad hides the controls until
 * the screen is touched again. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "overlay.h"
#include "touch_android.h"

enum { B_Z = 0x10, B_R = 0x20, B_L = 0x40, B_A = 0x100, B_B = 0x200, B_START = 0x1000 };
enum { C_JUMP, C_BOOST, C_DRIFT, C_TORNADO, C_PAUSE, C_MENU, C_COUNT };

typedef struct { float cx, cy, r; int icon; uint16_t buttons; float tint[3]; } Control;

#define MAX_FINGERS 10
typedef struct {
    SDL_FingerID id;
    int used;
    int role;      /* 0 stick, 1 buttons, 2 the open menu, 3 menu button */
    float x, y;    /* pixels */
} Finger;

static Control g_c[C_COUNT];
static int g_w, g_h;
static Finger g_f[MAX_FINGERS];
static int g_visible = 1;
static int g_stick_finger = -1;
static float g_base_x, g_base_y;       /* the stick's centre while held */
static float g_stick_vx, g_stick_vy;   /* -1..1, y down */
static unsigned g_pressed;             /* bit per control */
static uint64_t g_start_until;         /* PAUSE held at least this long (the game polls once a frame) */
static float g_ov[4] = {0, 0, 1, 1};   /* the menu image on screen */

static void layout(int w, int h) {
    if (w == g_w && h == g_h) return;
    g_w = w;
    g_h = h;
    float H = (float)h, W = (float)w, mx = 0.09f * H;
    float jx = W - mx - 0.15f * H, jy = H - 0.21f * H;
    g_c[C_JUMP] = (Control){jx, jy, 0.12f * H, 1, B_A, {0.35f, 0.75f, 1.0f}};
    g_c[C_BOOST] = (Control){jx - 0.28f * H, H - 0.13f * H, 0.09f * H, 2, B_B, {1.0f, 0.70f, 0.25f}};
    g_c[C_DRIFT] = (Control){jx + 0.01f * H, jy - 0.28f * H, 0.09f * H, 3, B_R, {0.55f, 1.0f, 0.55f}};
    g_c[C_TORNADO] = (Control){jx - 0.25f * H, jy - 0.24f * H, 0.065f * H, 4, B_L | B_R, {0.85f, 0.60f, 1.0f}};
    g_c[C_PAUSE] = (Control){W - mx - 0.05f * H, 0.085f * H, 0.05f * H, 5, B_START, {0.9f, 0.9f, 0.9f}};
    g_c[C_MENU] = (Control){mx + 0.05f * H, 0.085f * H, 0.05f * H, 6, 0, {0.9f, 0.9f, 0.9f}};
    rt_log("[touch] layout for %dx%d", w, h);
}

static float stick_r(void) { return 0.16f * (float)g_h; }
static void stick_home(float* x, float* y) {  /* where the base is shown while no thumb is on it */
    *x = 0.09f * (float)g_h + 0.22f * (float)g_h;
    *y = (float)g_h - 0.25f * (float)g_h;
}

/* the control under a point (generous: 1.25 x its radius), nearest centre first; -1 if none */
static int hit(float x, float y, int include_top) {
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < C_COUNT; i++) {
        if (!include_top && (i == C_PAUSE || i == C_MENU)) continue;
        float dx = x - g_c[i].cx, dy = y - g_c[i].cy, d = dx * dx + dy * dy, rr = g_c[i].r * 1.25f;
        if (d <= rr * rr && d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void recompute_buttons(void) {
    unsigned p = 0;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (!g_f[i].used || g_f[i].role != 1) continue;
        int c = hit(g_f[i].x, g_f[i].y, 1);
        if (c >= 0 && c != C_MENU) p |= 1u << c;
    }
    if ((p & (1u << C_PAUSE)) && !(g_pressed & (1u << C_PAUSE))) g_start_until = platform_now_ns() + 120000000ull;
    if ((p & ~g_pressed) & ((1u << C_JUMP) | (1u << C_BOOST) | (1u << C_DRIFT) | (1u << C_TORNADO)))
        ;  /* (haptic tick: later, with the phone's vibrator) */
    g_pressed = p;
}

static void stick_update(float x, float y) {
    float R = stick_r(), dx = x - g_base_x, dy = y - g_base_y, m = sqrtf(dx * dx + dy * dy);
    if (m > 1.6f * R) {  /* dragged far: the base follows the thumb */
        g_base_x = x - dx / m * 1.6f * R;
        g_base_y = y - dy / m * 1.6f * R;
        dx = x - g_base_x;
        dy = y - g_base_y;
        m = 1.6f * R;
    }
    float v = m / R;
    const float dead = 0.12f;
    if (v <= dead) {
        g_stick_vx = g_stick_vy = 0;
        return;
    }
    float s = (v - dead) / (1.0f - dead);
    if (s > 1.0f) s = 1.0f;
    g_stick_vx = dx / m * s;
    g_stick_vy = dy / m * s;
}

static int finger_slot(SDL_FingerID id, int make) {
    for (int i = 0; i < MAX_FINGERS; i++)
        if (g_f[i].used && g_f[i].id == id) return i;
    if (!make) return -1;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (!g_f[i].used) {
            memset(&g_f[i], 0, sizeof g_f[i]);
            g_f[i].used = 1;
            g_f[i].id = id;
            return i;
        }
    return -1;
}

static void to_overlay(float x, float y, float* ox, float* oy) {
    *ox = (x - g_ov[0]) / (g_ov[2] - g_ov[0]) * OVERLAY_W;
    *oy = (y - g_ov[1]) / (g_ov[3] - g_ov[1]) * OVERLAY_H;
}

void touch_set_overlay_rect(float x0, float y0, float x1, float y1) {
    g_ov[0] = x0; g_ov[1] = y0; g_ov[2] = x1 > x0 ? x1 : x0 + 1; g_ov[3] = y1 > y0 ? y1 : y0 + 1;
}

void touch_event(const SDL_Event* e, int win_w, int win_h) {
    if (win_w <= 0 || win_h <= 0) return;
    layout(win_w, win_h);
    if (e->type == SDL_CONTROLLERBUTTONDOWN || (e->type == SDL_CONTROLLERAXISMOTION && abs(e->caxis.value) > 16000)) {
        if (g_visible) rt_log("[touch] a controller is in use: touch controls hidden until the screen is touched");
        g_visible = 0;
        return;
    }
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP) return;
    float x = e->tfinger.x * (float)win_w, y = e->tfinger.y * (float)win_h;
    if (e->type == SDL_FINGERDOWN) {
        if (!g_visible) rt_log("[touch] touch controls shown");
        g_visible = 1;
        int s = finger_slot(e->tfinger.fingerId, 1);
        if (s < 0) return;
        g_f[s].x = x;
        g_f[s].y = y;
        int c = hit(x, y, 1);
        if (c == C_MENU) {
            g_f[s].role = 3;
            overlay_menu_toggle();
        } else if (overlay_menu_open()) {
            g_f[s].role = 2;
            float ox, oy;
            to_overlay(x, y, &ox, &oy);
            overlay_pointer(ox, oy, 1);
        } else if (c >= 0) {
            g_f[s].role = 1;
        } else if (x < (float)win_w * 0.5f && y > 0.18f * (float)win_h && g_stick_finger < 0) {
            g_f[s].role = 0;
            g_stick_finger = s;
            /* the base where the thumb landed, kept fully on screen */
            float R = stick_r(), m = 0.09f * (float)win_h;
            g_base_x = x < m + R * 0.6f ? m + R * 0.6f : x;
            g_base_y = y > (float)win_h - R * 0.6f ? (float)win_h - R * 0.6f : y;
            stick_update(x, y);
        } else {
            g_f[s].role = 1;  /* the right side's empty space: a thumb may slide onto a button from here */
        }
    } else {
        int s = finger_slot(e->tfinger.fingerId, 0);
        if (s < 0) return;
        g_f[s].x = x;
        g_f[s].y = y;
        if (g_f[s].role == 0) {
            if (e->type == SDL_FINGERUP) {
                g_stick_finger = -1;
                g_stick_vx = g_stick_vy = 0;
            } else {
                stick_update(x, y);
            }
        } else if (g_f[s].role == 2) {
            float ox, oy;
            to_overlay(x, y, &ox, &oy);
            overlay_pointer(ox, oy, e->type != SDL_FINGERUP);
        }
        if (e->type == SDL_FINGERUP) g_f[s].used = 0;
    }
    recompute_buttons();
}

void touch_apply(GcPad* p) {
    if (!g_w) return;
    unsigned pressed = g_pressed;
    if (platform_now_ns() < g_start_until) pressed |= 1u << C_PAUSE;
    for (int i = 0; i < C_COUNT; i++)
        if (pressed & (1u << i)) p->buttons |= g_c[i].buttons;
    if (p->buttons & B_L) p->trig_l = 0xFF;
    if (p->buttons & B_R) p->trig_r = 0xFF;
    if (g_stick_finger >= 0 && (g_stick_vx != 0 || g_stick_vy != 0)) {
        int sx = 0x80 + (int)lroundf(g_stick_vx * 100.0f), sy = 0x80 - (int)lroundf(g_stick_vy * 100.0f);
        p->stick_x = (uint8_t)(sx < 1 ? 1 : sx > 255 ? 255 : sx);
        p->stick_y = (uint8_t)(sy < 1 ? 1 : sy > 255 ? 255 : sy);
    }
}

int touch_shapes(TouchShape* out, int max, int win_w, int win_h) {
    if (win_w <= 0 || win_h <= 0 || max < C_COUNT + 2) return 0;
    layout(win_w, win_h);
    int n = 0;
    int menu = overlay_menu_open();
    static int off = -1;  /* SR_TOUCH=0: never drawn (picture comparisons with PC captures) */
    if (off < 0) off = getenv("SR_TOUCH") && atoi(getenv("SR_TOUCH")) == 0;
    if ((!g_visible || off) && !menu) return 0;
    /* the button pictures (android/touch_icons), slightly see-through so the race stays visible; a pressed
     * one is more solid and a little smaller */
    static const int k_img[C_COUNT] = {TOUCH_IMG_JUMP, TOUCH_IMG_BOOST, TOUCH_IMG_DRIFT, TOUCH_IMG_TORNADO, -1, -1};
    for (int i = 0; i < C_COUNT; i++) {
        if (menu && i != C_MENU) continue;  /* only the button that closes it while the menu is open */
        int pr = (g_pressed >> i) & 1;
        float r = g_c[i].r * (pr && k_img[i] >= 0 ? 0.93f : 1.0f);
        float a = k_img[i] >= 0 ? (pr ? 0.85f : 0.55f) : (pr ? 0.95f : 0.6f);
        out[n++] = (TouchShape){g_c[i].cx, g_c[i].cy, r, 0, g_c[i].icon, pr, k_img[i], a,
                                {g_c[i].tint[0], g_c[i].tint[1], g_c[i].tint[2]}};
    }
    if (!menu) {
        float bx, by, R = stick_r();
        if (g_stick_finger >= 0) {
            bx = g_base_x;
            by = g_base_y;
        } else {
            stick_home(&bx, &by);
        }
        int held = g_stick_finger >= 0;
        out[n++] = (TouchShape){bx, by, R, 1, 0, held, TOUCH_IMG_STICK_BASE, held ? 0.7f : 0.45f, {0.8f, 0.85f, 1.0f}};
        float kx = bx + g_stick_vx * R * 0.55f, ky = by + g_stick_vy * R * 0.55f;  /* the knob stays inside the gate */
        out[n++] = (TouchShape){kx, ky, 0.5f * R, 2, 0, held, TOUCH_IMG_STICK_KNOB, held ? 0.85f : 0.5f, {0.8f, 0.85f, 1.0f}};
    }
    return n;
}
