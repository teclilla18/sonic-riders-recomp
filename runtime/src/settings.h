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
    int pc_scale;       /* PC: internal resolution, 1-4 x 640x528 (2 = the old fixed 2x) */
    int msaa;           /* PC: anti-aliasing samples per pixel, 1 (off), 2, 4 or 8 */
    /* PC: bloom and colour grading of the 3D picture (render_gl.c post_process; the HUD is drawn after it) */
    int bloom;          /* glow strength in percent, 0 (off)..100 */
    int grade_bright;   /* brightness -50..50 (0 = as is) */
    int grade_contrast; /* contrast in percent, 50..150 (100 = as is) */
    int grade_sat;      /* saturation in percent, 0..200 (100 = as is) */
    int grade_warm;     /* warmth -50 (cooler, bluer)..50 (warmer, redder) */
    int char_light;     /* PC: 1 = lighting of lit 3D draws per pixel (gx.frag PERPIXEL_LIGHT), 0 = per vertex */
    int pc_fps;         /* PC: pictures per second, with in-between pictures (render_gl.c): 0 = the game's own
                           (60, or 30 in its 30 fps mode), PC_FPS_UNLIMITED, or one of settings_pc_fps_presets */
    int pad_background; /* PC: controllers keep working while the window is not focused (SDL hint) */
    int sharpness;      /* PC: FSR 1 RCAS sharpening of the final picture in percent, 0 (off)..100 */
    int update_check;   /* PC: ask GitHub at start-up whether a newer release exists (update_check.c) */
    int ring_cap_999;   /* races: a racer holds up to 999 rings (default) instead of the original 100
                           (g_ring_cap, read by the cap checks in config/GXEE8P/patches.txt) */
} Settings;

enum { FR_60, FR_AUTO, FR_30, FR_MODES };
enum { PC_FPS_UNLIMITED = -1 };
/* the PC frame rate presets (F1 > Performance): 30 = the game's 30 fps mode, 60 = as the game draws,
 * the others with in-between pictures; 0-terminated, PC_FPS_UNLIMITED last */
extern const int settings_pc_fps_presets[];
int settings_pc_fps_valid(int v);

enum { RES_1X, RES_1_5X, RES_2X, RES_1_25X, RES_MODES };  /* saved by number: 1.25x was added last (2026-11-14) */

enum { FOV_MIN = 80, FOV_MAX = 130 };
enum { PC_SCALE_MIN = 1, PC_SCALE_MAX = 4 };  /* 5x-6x tanked the fps on the maintainer's PC (2026-11-07) */

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
/* the ring cap from a recording or the online host (999 or 100): game state, kept for this run */
void settings_fix_ring_cap(int cap);
int settings_ring_cap(void);  /* the cap in effect: 999 or 100 */
int settings_wide_k_fixed(void);
const char* settings_resolution_name(int res);
const char* settings_framerate_name(int mode);
float settings_internal_scale(void);  /* 1, 1.25, 1.5 or 2 */

#ifdef __cplusplus
}
#endif
