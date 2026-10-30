/* The settings menu and FPS counter drawn over the game's picture (runtime/src/menu.cpp: Dear ImGui,
 * laid out like SwitchWakerHD's settings, in Sonic colours). Hold Minus (Switch) / Back or F1
 * (desktop) for half a second to open it; while it is open the game sees no buttons. L / R switch
 * tabs, the D-pad moves, A chooses, B goes back or closes.
 * The image is drawn in software (menu_raster.cpp) and composited by the backend over the presented
 * picture, scaled to the whole screen. Everything here runs on the render thread (which also pumps
 * input), except overlay_set_fps. On the Switch the menu has no Quit Game (HOME closes games). */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OVERLAY_W 1280
#define OVERLAY_H 720

enum { OV_UP = 1, OV_DOWN = 2, OV_LEFT = 4, OV_RIGHT = 8, OV_ACCEPT = 16, OV_BACK = 32, OV_MENU = 64, OV_TAB_L = 128, OV_TAB_R = 256 };

/* each input poll, with the OV_* buttons held: 1 while the game must see a neutral controller */
int overlay_input(unsigned held);
/* the overlay image (OVERLAY_W x OVERLAY_H RGBA, premultiplied alpha) or NULL when there is nothing to
 * show; *version changes whenever the pixels do. box (may be NULL) gets the rectangle x0, y0, x1, y1
 * outside which the image is transparent (all 0: nothing to draw) */
const uint8_t* overlay_image(unsigned* version, int box[4]);
void overlay_set_fps(float fps);  /* any thread */
/* the menu's "Save screenshot": 1 once after it was chosen (the backend saves the next picture) */
int overlay_take_screenshot_request(void);
void overlay_request_screenshot(void);
/* the menu's "Controllers": 1 once after it was chosen (Switch: show the system's controller screen) */
int overlay_take_controllers_request(void);
/* the menu's "Quit game" (PC): 1 once after it was confirmed (the platform exits as when the window closes) */
int overlay_take_quit_request(void);

#ifdef __cplusplus
}
#endif
