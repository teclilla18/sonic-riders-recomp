/* User options (the hold-Minus menu, overlay.c), saved as settings.ini next to the memory card. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* SCREEN_FIT: the window's (or screen's) aspect, 4:3 to 32:9, with a wider picture (not stretched) */
/* 2 was "16:9 stretched" (removed 2026-11-06): a saved 2 reads as SCREEN_WIDE */
enum { SCREEN_4_3, SCREEN_WIDE, SCREEN_REMOVED_2, SCREEN_FIT, SCREEN_MODES };

typedef struct Settings {
    int screen;    /* SCREEN_*: 4:3 letterboxed, 16:9 widescreen (patched cameras), 16:9 stretched, fit window */
    int show_fps;  /* frame counter in the corner */
    int resolution;  /* RES_*: internal resolution of the Switch renderer (the PC build keeps 2x) */
    int chaos_emerald;  /* the Chaos Emerald gear (Super Sonic) is unlocked; nothing else is (gx_render.c) */
    int fullscreen;     /* PC: borderless fullscreen window (F11 / Alt+Enter, or the menu); unused on the Switch */
    int framerate;      /* FR_*: what happens when a frame takes longer than a 60 Hz retrace (frame_pacing.c) */
    int handheld_gpu;   /* Switch: Nintendo's 384 MHz handheld GPU configuration (platform_set_handheld_gpu) */
    int hud_stretch;    /* SCREEN_WIDE: 2D (HUD, menus) stretched to the width instead of 4:3 in the middle;
                           SCREEN_FIT always moves the HUD to the screen edges */
    int fov;            /* field of view in percent of the game's (FOV_MIN..FOV_MAX, 100 = original) */
} Settings;

enum { FR_60, FR_AUTO, FR_30, FR_MODES };

enum { RES_1X, RES_1_5X, RES_2X, RES_MODES };

enum { FOV_MIN = 80, FOV_MAX = 130 };

extern Settings g_settings;
extern volatile double g_aspect_scale;  /* camera aspect multiplier (config/GXEE8P/patches.txt) */
extern volatile double g_fov_scale;     /* camera field of view multiplier (config/GXEE8P/patches.txt) */

void settings_load(void);   /* at start-up: reads the file, applies */
int settings_handheld_pc(void);  /* Steam Deck / gamescope (first-run defaults, window size) */
void settings_apply(void);  /* after a change: applies, saves and logs */
float settings_display_aspect(void);  /* of the presented picture: 4/3, 16/9 or the window's */
/* the wide modes draw a wider EFB instead of stretching: its width is 640 x this (1 = the original);
 * the backends scale viewports, scissors and copies by it (gx_render.c frame start) */
float settings_wide_k(void);
/* backends: the window's or screen's aspect (width / height), for SCREEN_FIT; cheap when unchanged */
void settings_set_window_aspect(float aspect);
const char* settings_screen_name(int mode);
/* online sessions, recordings and replays (netplay.c): the camera aspect is game state, so it is fixed
 * for the whole run, the same on every machine (EFB width factor k; 0 = not fixed). Screen settings
 * and window size changes then only change how the picture is shown. */
void settings_fix_wide_k(float k);
int settings_wide_k_fixed(void);
const char* settings_resolution_name(int res);
const char* settings_framerate_name(int mode);
float settings_internal_scale(void);  /* 1, 1.5 or 2 */

#ifdef __cplusplus
}
#endif
