/* The settings menu and FPS counter: see overlay.h. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overlay.h"
#include "runtime.h"
#include "settings.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../third_party/stb_easy_font.h"
#pragma GCC diagnostic pop

#define HOLD_TICKS (40500000ull / 2)  /* Minus held this long opens the menu (0.5 s) */
#define TEXT_SCALE 2

enum { ITEM_SCREEN, ITEM_RES, ITEM_FPS, ITEM_CLOSE, ITEMS };

static uint8_t g_img[OVERLAY_W * OVERLAY_H * 4];
static unsigned g_version;
static int g_dirty = 1, g_open, g_sel;
static int g_gate;               /* closed, but buttons still held from the menu: kept from the game */
static unsigned g_prev;
static uint64_t g_menu_since;    /* rt_ticks() when Minus went down; 0 = not held */
static _Atomic int g_fps10;      /* frames per second x 10 */
static int g_shown_fps10 = -1;

void overlay_set_fps(float fps) { atomic_store(&g_fps10, (int)(fps * 10.0f + 0.5f)); }

static void close_menu(void) {
    g_open = 0;
    g_gate = 1;
    g_dirty = 1;
}

int overlay_input(unsigned held) {
    static int tested;
    if (!tested++ && getenv("SR_TEST_MENU")) {  /* debugging: start with the menu open */
        g_open = 1;
        g_dirty = 1;
    }
    unsigned pressed = held & ~g_prev;
    g_prev = held;
    if (!g_open) {
        if (g_gate) {
            if (held) return 1;
            g_gate = 0;
        }
        if (held & OV_MENU) {
            uint64_t now = rt_ticks();
            if (!g_menu_since) g_menu_since = now;
            if (now - g_menu_since >= HOLD_TICKS) {
                g_open = 1;
                g_sel = 0;
                g_dirty = 1;
                g_menu_since = 0;
                g_prev = held;  /* the Minus still held does not close it again */
                return 1;
            }
        } else {
            g_menu_since = 0;
        }
        return 0;
    }
    if (pressed & (OV_BACK | OV_MENU)) {
        close_menu();
        return 1;
    }
    if (pressed & OV_UP) g_sel = (g_sel + ITEMS - 1) % ITEMS;
    if (pressed & OV_DOWN) g_sel = (g_sel + 1) % ITEMS;
    int step = pressed & OV_LEFT ? -1 : pressed & (OV_RIGHT | OV_ACCEPT) ? 1 : 0;
    if (step) {
        switch (g_sel) {
        case ITEM_SCREEN: g_settings.screen = (g_settings.screen + SCREEN_MODES + step) % SCREEN_MODES; settings_apply(); break;
        case ITEM_RES: g_settings.resolution = (g_settings.resolution + RES_MODES + step) % RES_MODES; settings_apply(); break;
        case ITEM_FPS: g_settings.show_fps = !g_settings.show_fps; settings_apply(); break;
        case ITEM_CLOSE: if (pressed & OV_ACCEPT) close_menu(); break;
        default: break;
        }
    }
    if (pressed) g_dirty = 1;
    return 1;
}

/* ---- drawing ---- */
static void fill(int x0, int y0, int x1, int y1, uint32_t rgba) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > OVERLAY_W) x1 = OVERLAY_W;
    if (y1 > OVERLAY_H) y1 = OVERLAY_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) memcpy(g_img + ((size_t)y * OVERLAY_W + x) * 4, &rgba, 4);
}

static uint32_t rgba(int r, int g, int b, int a) { return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24; }

/* stb_easy_font quads (axis-aligned rectangles), scaled */
static void text(int x, int y, const char* s, uint32_t color) {
    static char vb[64 * 1024];
    int quads = stb_easy_font_print(0, 0, (char*)s, NULL, vb, sizeof vb);
    for (int q = 0; q < quads; q++) {
        const float* v = (const float*)(vb + q * 64);  /* 4 vertices of 16 bytes: x y z color */
        float x0 = v[0], y0 = v[1], x1 = v[0], y1 = v[1];
        for (int k = 1; k < 4; k++) {
            float vx = v[k * 4], vy = v[k * 4 + 1];
            if (vx < x0) x0 = vx;
            if (vx > x1) x1 = vx;
            if (vy < y0) y0 = vy;
            if (vy > y1) y1 = vy;
        }
        fill(x + (int)(x0 * TEXT_SCALE), y + (int)(y0 * TEXT_SCALE), x + (int)(x1 * TEXT_SCALE), y + (int)(y1 * TEXT_SCALE), color);
    }
}

static int text_w(const char* s) { return stb_easy_font_width((char*)s) * TEXT_SCALE; }

static void redraw(void) {
    memset(g_img, 0, sizeof g_img);
    if (g_settings.show_fps && g_shown_fps10 >= 0) {
        char t[32];
        snprintf(t, sizeof t, "%d.%d FPS", g_shown_fps10 / 10, g_shown_fps10 % 10);
        fill(4, 4, 12 + text_w(t), 30, rgba(0, 0, 0, 160));
        text(8, 8, t, rgba(255, 255, 255, 255));
    }
    if (!g_open) return;
    const int w = 520, h = 234, x0 = (OVERLAY_W - w) / 2, y0 = (OVERLAY_H - h) / 2;
    fill(x0, y0, x0 + w, y0 + h, rgba(10, 16, 40, 225));
    fill(x0, y0, x0 + w, y0 + 3, rgba(80, 160, 255, 255));
    text(x0 + 16, y0 + 14, "SONIC RIDERS - SETTINGS", rgba(80, 160, 255, 255));
    char lines[ITEMS][64];
    snprintf(lines[ITEM_SCREEN], 64, "Screen:  < %s >", settings_screen_name(g_settings.screen));
    snprintf(lines[ITEM_RES], 64, "Resolution:  < %s >", settings_resolution_name(g_settings.resolution));
    snprintf(lines[ITEM_FPS], 64, "FPS counter:  < %s >", g_settings.show_fps ? "On" : "Off");
    snprintf(lines[ITEM_CLOSE], 64, "Close");
    for (int i = 0; i < ITEMS; i++) {
        int y = y0 + 52 + i * 34;
        if (i == g_sel) fill(x0 + 8, y - 6, x0 + w - 8, y + 24, rgba(40, 70, 140, 255));
        text(x0 + 20, y, lines[i], i == g_sel ? rgba(255, 230, 80, 255) : rgba(230, 230, 230, 255));
    }
    text(x0 + 16, y0 + h - 26, "D-Pad: choose / change    B: close", rgba(150, 160, 190, 255));
}

const uint8_t* overlay_image(unsigned* version) {
    int fps10 = g_settings.show_fps ? atomic_load(&g_fps10) : -1;
    if (fps10 != g_shown_fps10) {
        g_shown_fps10 = fps10;
        g_dirty = 1;
    }
    if (g_dirty) {
        redraw();
        g_dirty = 0;
        g_version++;
    }
    *version = g_version;
    return g_open || g_settings.show_fps ? g_img : NULL;
}
