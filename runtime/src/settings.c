/* User options: see settings.h. The file is plain `key=value` lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "settings.h"

Settings g_settings = {SCREEN_4_3, 0, RES_1X, 1, 0, FR_AUTO, 1};
volatile double g_aspect_scale = 1.0;

const char* settings_screen_name(int mode) {
    static const char* const names[SCREEN_MODES] = {"4:3 (original)", "16:9 widescreen", "16:9 stretched"};
    return mode >= 0 && mode < SCREEN_MODES ? names[mode] : "?";
}

const char* settings_resolution_name(int res) {
    static const char* const names[RES_MODES] = {"1x (640x528, fastest)", "1.5x (960x792)", "2x (1280x1056)"};
    return res >= 0 && res < RES_MODES ? names[res] : "?";
}

const char* settings_framerate_name(int mode) {
    static const char* const names[FR_MODES] = {"60 fps (original)", "Auto (keep speed)", "30 fps (full speed)"};
    return mode >= 0 && mode < FR_MODES ? names[mode] : "?";
}

float settings_internal_scale(void) { return g_settings.resolution == RES_2X ? 2.0f : g_settings.resolution == RES_1_5X ? 1.5f : 1.0f; }

float settings_display_aspect(void) { return g_settings.screen == SCREEN_4_3 ? 4.0f / 3.0f : 16.0f / 9.0f; }

static void apply(void) {
    /* widescreen: the cameras' aspect ratio from 4:3 to 16:9 */
    g_aspect_scale = g_settings.screen == SCREEN_WIDE ? (16.0 / 9.0) / (4.0 / 3.0) : 1.0;
    platform_set_handheld_gpu(g_settings.handheld_gpu);
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
            if (!strcmp(line, "screen") && v >= 0 && v < SCREEN_MODES) g_settings.screen = v;
            else if (!strcmp(line, "show_fps")) g_settings.show_fps = v != 0;
            else if (!strcmp(line, "resolution") && v >= 0 && v < RES_MODES) g_settings.resolution = v;
            else if (!strcmp(line, "chaos_emerald")) g_settings.chaos_emerald = v != 0;
            else if (!strcmp(line, "fullscreen")) g_settings.fullscreen = v != 0;
            else if (!strcmp(line, "framerate") && v >= 0 && v < FR_MODES) g_settings.framerate = v;
            else if (!strcmp(line, "handheld_gpu")) g_settings.handheld_gpu = v != 0;
        }
        fclose(f);
    }
    const char* fr = getenv("SR_FRAMERATE");  /* testing: 0 = 60 fps, 1 = Auto, 2 = 30 fps (not saved unless changed in the menu) */
    if (fr && atoi(fr) >= 0 && atoi(fr) < FR_MODES) g_settings.framerate = atoi(fr);
    apply();
    rt_log("[settings] screen: %s, fps counter %s, resolution %s, frame rate %s, Chaos Emerald gear %s",
           settings_screen_name(g_settings.screen), g_settings.show_fps ? "on" : "off",
           settings_resolution_name(g_settings.resolution), settings_framerate_name(g_settings.framerate),
           g_settings.chaos_emerald ? "unlocked" : "as in the save");
}

void settings_apply(void) {
    apply();
    game_mods_apply();
    FILE* f = fopen(platform_settings_path(), "w");
    if (!f) {
        rt_log("[settings] cannot write %s", platform_settings_path());
        return;
    }
    fprintf(f, "screen=%d\nshow_fps=%d\nresolution=%d\nchaos_emerald=%d\nfullscreen=%d\nframerate=%d\nhandheld_gpu=%d\n", g_settings.screen,
            g_settings.show_fps, g_settings.resolution, g_settings.chaos_emerald, g_settings.fullscreen, g_settings.framerate,
            g_settings.handheld_gpu);
    fclose(f);
}
