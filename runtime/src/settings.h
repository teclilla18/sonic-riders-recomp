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
    int fullscreen;     /* PC: borderless fullscreen window (F11 / Alt+Enter, or the menu); unused on the Switch */
    int framerate;      /* FR_*: what happens when a frame takes longer than a 60 Hz retrace (frame_pacing.c) */
    int handheld_gpu;   /* Switch: Nintendo's 384 MHz handheld GPU configuration (platform_set_handheld_gpu) */
    int dynamic_res;    /* Switch: lower the internal resolution in busy scenes to hold the frame rate (render_deko.c) */
} Settings;

enum { FR_60, FR_AUTO, FR_30, FR_MODES };

enum { RES_1X, RES_1_5X, RES_2X, RES_MODES };

extern Settings g_settings;
extern volatile double g_aspect_scale;  /* camera aspect multiplier (config/GXEE8P/patches.txt) */

void settings_load(void);   /* at start-up: reads the file, applies */
void settings_apply(void);  /* after a change: applies, saves and logs */
float settings_display_aspect(void);  /* of the presented picture: 4/3 or 16/9 */
const char* settings_screen_name(int mode);
const char* settings_resolution_name(int res);
const char* settings_framerate_name(int mode);
float settings_internal_scale(void);  /* 1, 1.5 or 2 */

#ifdef __cplusplus
}
#endif
