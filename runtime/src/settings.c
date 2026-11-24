/* User options: see settings.h. The file is plain `key=value` lines. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "runtime.h"
#include "settings.h"

/* the Chaos Emerald unlock starts off: players who want to earn it themselves (user, 2026-11-07) */
/* the 999 ring cap starts on, the original 100 one menu switch away (user, 2026-11-12) */
Settings g_settings = {SCREEN_4_3, 0, RES_1X, 0, 0, FR_AUTO, 1, 0, 100, 2, 1, 0, 0, 100, 100, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1};
volatile int g_ring_cap = 999;  /* patches.txt: the most rings a racer holds */
static int g_fixed_ring_cap;    /* settings_fix_ring_cap, 0 = the setting's */
static int g_boost_hold;        /* Hold to boost in effect */
static int g_fixed_boost_hold = -1;  /* settings_fix_boost_hold, -1 = the setting's */
static int g_ring_replenish;    /* Ring Replenish in effect */
static int g_fixed_ring_replenish = -1;  /* settings_fix_ring_replenish, -1 = the setting's */
static int g_fixed_super_flight = -1;    /* settings_fix_super_flight: an online room's (the host's) or a recording's */
volatile double g_aspect_scale = 1.0;
volatile double g_fov_scale = 1.0;
static volatile float g_window_aspect = 16.0f / 9.0f;
static volatile float g_wide_k = 1.0f;
static float g_fixed_k;  /* settings_fix_wide_k */

const char* settings_screen_name(int mode) {
    static const char* const names[SCREEN_MODES] = {"4:3 (original)", "16:9 widescreen", "16:9 stretched (removed)", "Fit to screen"};
    return mode >= 0 && mode < SCREEN_MODES ? names[mode] : "?";
}

const char* settings_resolution_name(int res) {
    static const char* const names[RES_MODES] = {"1x (640x528, fastest)", "1.5x (960x792)", "2x (1280x1056)",
                                                         "1.25x (800x660)"};
    return res >= 0 && res < RES_MODES ? names[res] : "?";
}

const int settings_pc_fps_presets[] = {30, 45, 60, 120, 144, 180, 240, PC_FPS_UNLIMITED, 0};
int settings_pc_fps_valid(int v) {
    if (v == 0) return 1;
    for (int i = 0; settings_pc_fps_presets[i]; i++)
        if (settings_pc_fps_presets[i] == v) return v != 30 && v != 60;  /* those two are pc_fps 0 */
    return 0;
}

const char* settings_framerate_name(int mode) {
    static const char* const names[FR_MODES] = {"60 fps (30 when busy)", "Auto (keep speed)", "30 fps (full speed)"};
    return mode >= 0 && mode < FR_MODES ? names[mode] : "?";
}

float settings_internal_scale(void) { return g_settings.resolution == RES_2X ? 2.0f : g_settings.resolution == RES_1_5X ? 1.5f : g_settings.resolution == RES_1_25X ? 1.25f : 1.0f; }

/* an aspect from 4:3 to 32:9, rounded so the EFB is a whole number of
 * (even) pixels wide: 640 x k */
static float round_aspect(float a) {
    if (!(a >= 4.0f / 3.0f)) a = 4.0f / 3.0f;
    if (a > 32.0f / 9.0f) a = 32.0f / 9.0f;
    float w = floorf(640.0f * a / (4.0f / 3.0f) / 2.0f + 0.5f) * 2.0f;
    return w / 640.0f * (4.0f / 3.0f);
}

/* the shape this machine's own settings and screen ask for */
static float own_display_aspect(void) {
    switch (g_settings.screen) {
    case SCREEN_4_3: return 4.0f / 3.0f;
    /* 16:9 rounded like the fit mode's, so both give the game the same camera aspect (online play) */
    case SCREEN_FIT: return round_aspect(g_window_aspect);
    case SCREEN_WIDE: return round_aspect(16.0f / 9.0f);
    default: return 16.0f / 9.0f;
    }
}

/* of the presented picture, limited by the camera used for game-side visibility */
float settings_display_aspect(void) { return g_fixed_k >= 1.0f ? 4.0f / 3.0f * g_wide_k : own_display_aspect(); }
float settings_wide_k(void) { return g_wide_k; }
/* the camera aspect the game itself uses (game state: the room's in an online session, a recording's in a replay) */
float settings_game_k(void) { return (float)g_aspect_scale; }

/* the log says what was on, at start-up and after each change in the menu (hardware test reports) */
static void settings_log(const char* when) {
    rt_log("[settings] %sscreen: %s (%.3f), HUD %s, field of view %d%%, fps counter %s, resolution %s, frame rate %s, PC pictures %d/s, "
           "sharpness %d%%, pad in background %s, handheld graphics %s, Chaos Emerald gear %s, ring cap %d, auto spin %s, hold to boost %s",
           when, settings_screen_name(g_settings.screen), settings_display_aspect(),
           g_settings.screen == SCREEN_FIT ? "at the edges" : g_settings.hud_stretch ? "stretched" : "4:3", g_settings.fov, g_settings.show_fps ? "on" : "off",
           settings_resolution_name(g_settings.resolution),
           settings_framerate_name(g_settings.framerate), g_settings.pc_fps,
           g_settings.sharpness, g_settings.pad_background ? "on" : "off", g_settings.handheld_gpu ? "384 MHz" : "system default",
           g_settings.chaos_emerald ? "unlocked" : "as in the save", (int)g_ring_cap, g_settings.auto_spin ? "on" : "off",
           g_settings.boost_hold ? "on" : "off");
}

static void apply_view(void) {
    /* A fixed camera covers the whole supported view. The renderer narrows perspective projection
     * to the local window shape while retaining the wider game-side visibility checks. */
    int wide = g_settings.screen == SCREEN_WIDE || g_settings.screen == SCREEN_FIT;
    float own = wide ? own_display_aspect() / (4.0f / 3.0f) : 1.0f;
    g_wide_k = g_fixed_k >= 1.0f ? fminf(own, g_fixed_k) : own;
    g_aspect_scale = g_fixed_k >= 1.0f ? g_fixed_k : own;
    g_fov_scale = g_settings.fov / 100.0;
}

static void apply(void) {
    apply_view();
    g_ring_cap = g_fixed_ring_cap ? g_fixed_ring_cap : g_settings.ring_cap_999 ? 999 : 100;
    /* part of Riders Plus: only with it (an online host / a recording: its value) */
    g_boost_hold = g_fixed_boost_hold >= 0 ? g_fixed_boost_hold : g_settings.boost_hold && g_ring_cap != 100;
    g_ring_replenish = g_fixed_ring_replenish >= 0 ? g_fixed_ring_replenish
                                                   : g_settings.ring_replenish && g_ring_cap != 100;
    platform_set_handheld_gpu(g_settings.handheld_gpu);
}

/* a Steam Deck or another gamescope session (Linux): Steam sets SteamDeck=1, gamescope its display */
int settings_handheld_pc(void) {
    const char* deck = getenv("SteamDeck");
    return (deck && atoi(deck) == 1) || getenv("GAMESCOPE_WAYLAND_DISPLAY") != NULL;
}

void settings_load(void) {
    FILE* f = fopen(platform_settings_path(), "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            int v = atoi(eq + 1);
            if (!strcmp(line, "online_name")) {
                void lan_clean_name(char*, const char*);
                lan_clean_name(g_settings.online_name, eq + 1);
            }
            else if (!strcmp(line, "screen") && v >= 0 && v < SCREEN_MODES) g_settings.screen = v;
            else if (!strcmp(line, "show_fps")) g_settings.show_fps = v != 0;
            else if (!strcmp(line, "resolution") && v >= 0 && v < RES_MODES) g_settings.resolution = v;
            /* a new key: older builds saved "chaos_emerald" as on by default */
            else if (!strcmp(line, "chaos_emerald_shop")) g_settings.chaos_emerald = v != 0;
            else if (!strcmp(line, "fullscreen")) g_settings.fullscreen = v != 0;
            else if (!strcmp(line, "framerate") && v >= 0 && v < FR_MODES) g_settings.framerate = v;
            else if (!strcmp(line, "handheld_gpu")) g_settings.handheld_gpu = v != 0;
            else if (!strcmp(line, "hud_stretch")) g_settings.hud_stretch = v != 0;
            else if (!strcmp(line, "fov") && v >= FOV_MIN && v <= FOV_MAX) g_settings.fov = v;
            else if (!strcmp(line, "pc_scale") && v >= PC_SCALE_MIN) g_settings.pc_scale = v > PC_SCALE_MAX ? PC_SCALE_MAX : v;
            else if (!strcmp(line, "msaa") && (v == 1 || v == 2 || v == 4 || v == 8)) g_settings.msaa = v;
            else if (!strcmp(line, "bloom") && v >= 0 && v <= 100) g_settings.bloom = v;
            else if (!strcmp(line, "grade_bright") && v >= -50 && v <= 50) g_settings.grade_bright = v;
            else if (!strcmp(line, "grade_contrast") && v >= 50 && v <= 150) g_settings.grade_contrast = v;
            else if (!strcmp(line, "grade_sat") && v >= 0 && v <= 200) g_settings.grade_sat = v;
            else if (!strcmp(line, "grade_warm") && v >= -50 && v <= 50) g_settings.grade_warm = v;
            else if (!strcmp(line, "char_light")) g_settings.char_light = v != 0;
            else if (!strcmp(line, "pc_fps") && settings_pc_fps_valid(v)) g_settings.pc_fps = v;
            else if (!strcmp(line, "pad_background")) g_settings.pad_background = v != 0;
            else if (!strcmp(line, "sharpness") && v >= 0 && v <= 100) g_settings.sharpness = v;
            else if (!strcmp(line, "update_check")) g_settings.update_check = v != 0;
            else if (!strcmp(line, "ring_cap_999")) g_settings.ring_cap_999 = v != 0;
            else if (!strcmp(line, "auto_spin")) g_settings.auto_spin = v != 0;
            else if (!strcmp(line, "boost_hold")) g_settings.boost_hold = v != 0;
            else if (!strcmp(line, "ring_replenish")) g_settings.ring_replenish = v != 0;
            else if (!strcmp(line, "super_flight")) g_settings.super_flight = v != 0;
            else if (!strcmp(line, "no_ring_drain")) g_settings.no_ring_drain = v != 0;
        }
        fclose(f);
    } else if (settings_handheld_pc()) {
        /* first run on a Steam Deck (or under gamescope): fill the screen. gamescope scales the window to
         * the display, so a 4:3 picture got bars (gbatemp report, 2026-11-06) */
        g_settings.screen = SCREEN_FIT;
        g_settings.fullscreen = 1;
    }
    const char* fr = getenv("SR_FRAMERATE");  /* testing: 0 = 60 fps, 1 = Auto, 2 = 30 fps (not saved unless changed in the menu) */
    if (fr && atoi(fr) >= 0 && atoi(fr) < FR_MODES) g_settings.framerate = atoi(fr);
    const char* pf = getenv("SR_FPS");  /* testing: the PC picture rate (pc_fps; 0 = the game's, -1 = unlimited) */
    if (pf && settings_pc_fps_valid(atoi(pf))) g_settings.pc_fps = atoi(pf);
    const char* sh = getenv("SR_SHARPNESS");  /* testing: FSR sharpening 0-100 (not saved unless changed in the menu) */
    if (sh && atoi(sh) >= 0 && atoi(sh) <= 100) g_settings.sharpness = atoi(sh);
    /* testing, not saved unless changed in the menu: SR_BLOOM=0-100, SR_GRADE=brightness,contrast,saturation,warmth,
     * SR_RES=0-3 (1x / 1.5x / 2x / 1.25x; Switch and phones) */
    const char* bl = getenv("SR_BLOOM");
    if (bl && atoi(bl) >= 0 && atoi(bl) <= 100) g_settings.bloom = atoi(bl);
    const char* gr = getenv("SR_GRADE");
    if (gr && *gr)
        sscanf(gr, "%d,%d,%d,%d", &g_settings.grade_bright, &g_settings.grade_contrast, &g_settings.grade_sat,
               &g_settings.grade_warm);
    const char* rs = getenv("SR_RES");
    if (rs && atoi(rs) >= 0 && atoi(rs) < RES_MODES) g_settings.resolution = atoi(rs);
    /* testing, not saved unless changed in the menu: SR_SCREEN=0 or 3 (SCREEN_*), SR_FOV=80-130 */
    const char* sc = getenv("SR_SCREEN");
    if (sc && atoi(sc) >= 0 && atoi(sc) < SCREEN_MODES) g_settings.screen = atoi(sc);
    /* two screen options only (user, 2026-11-14): 4:3 and Fit to screen (HUD at the edges). The old "16:9
     * widescreen" (1) and "16:9 stretched" (2) read as Fit; the HUD is never stretched */
    if (g_settings.screen == SCREEN_WIDE || g_settings.screen == SCREEN_REMOVED_2) g_settings.screen = SCREEN_FIT;
    g_settings.hud_stretch = 0;
    const char* fv = getenv("SR_FOV");
    if (fv && atoi(fv) >= FOV_MIN && atoi(fv) <= FOV_MAX) g_settings.fov = atoi(fv);
    apply();
    settings_log("");
}

void settings_fix_wide_k(float k) {
    g_fixed_k = k >= 1.0f && k <= 8.0f ? k : 0.0f;
    apply_view();
    rt_log("[settings] camera aspect fixed for this run: %.4f (game k %.4f); this screen draws %.4f (EFB width x%.4f)",
           4.0f / 3.0f * (float)g_aspect_scale, (float)g_aspect_scale, 4.0f / 3.0f * g_wide_k, g_wide_k);
}

int settings_wide_k_fixed(void) { return g_fixed_k >= 1.0f; }

void settings_fix_ring_cap(int cap) {
    g_fixed_ring_cap = cap == 100 || cap == 999 ? cap : 0;
    apply();
    rt_log("[settings] ring cap fixed for this run: %d", (int)g_ring_cap);
}

int settings_ring_cap(void) { return g_ring_cap; }

void settings_fix_boost_hold(int on) {
    g_fixed_boost_hold = on != 0;
    apply();
    rt_log("[settings] hold to boost fixed for this run: %s", g_boost_hold ? "on" : "off");
}

int settings_boost_hold(void) { return g_boost_hold; }

void settings_fix_ring_replenish(int on) {
    g_fixed_ring_replenish = on != 0;
    apply();
    rt_log("[settings] ring replenish fixed for this run: %s", g_ring_replenish ? "on" : "off");
}

int settings_ring_replenish(void) { return g_ring_replenish; }
/* a cheat: never in online play, a recording or a replay (they all run on the game-driven clock) */
int settings_super_flight(void) {
    int ppc_detclock(void);
    if (g_fixed_super_flight >= 0) return g_fixed_super_flight && g_ring_cap != 100;
    return g_settings.super_flight && g_ring_cap != 100 && !ppc_detclock();
}
/* Flight only changes which animation Super Sonic plays, from game state every machine has, so a room can use it
 * as long as every machine uses the same value: the host's (WELCOME) or a recording's. */
int settings_super_flight_setting(void) { return g_settings.super_flight && g_ring_cap != 100; }
void settings_fix_super_flight(int on) {
    g_fixed_super_flight = on != 0;
    rt_log("[settings] Super Sonic flight fixed for this run: %s", on ? "on" : "off");
}
int settings_no_ring_drain(void) { int ppc_detclock(void); return g_settings.no_ring_drain && !ppc_detclock(); }
int settings_auto_spin(void) { return g_settings.auto_spin && g_ring_cap != 100; }  /* part of Riders Plus */

void settings_set_window_aspect(float aspect) {
    if (!(aspect > 0.1f) || fabsf(aspect - g_window_aspect) < 0.001f) return;
    g_window_aspect = aspect;
    if (g_settings.screen == SCREEN_FIT) {
        apply_view();
        rt_log("[settings] window aspect %.3f: picture %.3f, EFB width x%.3f", aspect, settings_display_aspect(), g_wide_k);
    }
}

void settings_apply(void) {
    apply();
    settings_log("changed: ");
    game_mods_apply();
    FILE* f = fopen(platform_settings_path(), "w");
    if (!f) {
        rt_log("[settings] cannot write %s", platform_settings_path());
        return;
    }
    fprintf(f, "screen=%d\nshow_fps=%d\nresolution=%d\nchaos_emerald_shop=%d\nfullscreen=%d\nframerate=%d\nhandheld_gpu=%d\nhud_stretch=%d\nfov=%d\npc_scale=%d\nmsaa=%d\nbloom=%d\ngrade_bright=%d\ngrade_contrast=%d\ngrade_sat=%d\ngrade_warm=%d\nchar_light=%d\npc_fps=%d\npad_background=%d\nsharpness=%d\nupdate_check=%d\nring_cap_999=%d\nauto_spin=%d\nboost_hold=%d\nring_replenish=%d\nno_ring_drain=%d\nsuper_flight=%d\n",
            g_settings.screen,
            g_settings.show_fps, g_settings.resolution, g_settings.chaos_emerald, g_settings.fullscreen, g_settings.framerate,
            g_settings.handheld_gpu, g_settings.hud_stretch, g_settings.fov, g_settings.pc_scale, g_settings.msaa,
            g_settings.bloom, g_settings.grade_bright, g_settings.grade_contrast, g_settings.grade_sat, g_settings.grade_warm,
            g_settings.char_light, g_settings.pc_fps, g_settings.pad_background, g_settings.sharpness, g_settings.update_check,
            g_settings.ring_cap_999, g_settings.auto_spin, g_settings.boost_hold,
            g_settings.ring_replenish, g_settings.no_ring_drain, g_settings.super_flight);
    fprintf(f, "online_name=%s\n", g_settings.online_name);
    fclose(f);
}
