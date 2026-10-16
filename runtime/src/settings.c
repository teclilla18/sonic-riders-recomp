/* User options: see settings.h. The file is plain `key=value` lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "settings.h"

Settings g_settings = {SCREEN_4_3, 0};
volatile double g_aspect_scale = 1.0;

const char* settings_screen_name(int mode) {
    static const char* const names[SCREEN_MODES] = {"4:3 (original)", "16:9 widescreen", "16:9 stretched"};
    return mode >= 0 && mode < SCREEN_MODES ? names[mode] : "?";
}

float settings_display_aspect(void) { return g_settings.screen == SCREEN_4_3 ? 4.0f / 3.0f : 16.0f / 9.0f; }

static void apply(void) {
    /* widescreen: the cameras' aspect ratio from 4:3 to 16:9 */
    g_aspect_scale = g_settings.screen == SCREEN_WIDE ? (16.0 / 9.0) / (4.0 / 3.0) : 1.0;
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
        }
        fclose(f);
    }
    apply();
    rt_log("[settings] screen: %s, fps counter %s", settings_screen_name(g_settings.screen),
           g_settings.show_fps ? "on" : "off");
}

void settings_apply(void) {
    apply();
    FILE* f = fopen(platform_settings_path(), "w");
    if (!f) {
        rt_log("[settings] cannot write %s", platform_settings_path());
        return;
    }
    fprintf(f, "screen=%d\nshow_fps=%d\n", g_settings.screen, g_settings.show_fps);
    fclose(f);
}
