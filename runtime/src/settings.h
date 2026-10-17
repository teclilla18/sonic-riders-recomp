/* User options (the hold-Minus menu, overlay.c), saved as settings.ini next to the memory card. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum { SCREEN_4_3, SCREEN_WIDE, SCREEN_STRETCH, SCREEN_MODES };

typedef struct Settings {
    int screen;    /* SCREEN_*: 4:3 letterboxed, 16:9 widescreen (patched cameras), 16:9 stretched */
    int show_fps;  /* frame counter in the corner */
    int resolution;  /* RES_*: internal resolution of the Switch renderer (the PC build keeps 2x) */
    int chaos_emerald;  /* the Chaos Emerald gear (Super Sonic) is unlocked; nothing else is (gx_render.c) */
} Settings;

enum { RES_1X, RES_1_5X, RES_2X, RES_MODES };

extern Settings g_settings;
extern volatile double g_aspect_scale;  /* camera aspect multiplier (config/GXEE8P/patches.txt) */

void settings_load(void);   /* at start-up: reads the file, applies */
void settings_apply(void);  /* after a change: applies and saves */
float settings_display_aspect(void);  /* of the presented picture: 4/3 or 16/9 */
const char* settings_screen_name(int mode);
const char* settings_resolution_name(int res);
float settings_internal_scale(void);  /* 1, 1.5 or 2 */

#ifdef __cplusplus
}
#endif
