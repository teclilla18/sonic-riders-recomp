/* The settings menu and FPS counter drawn over the game's picture. Hold Minus (Switch) / Back or F1
 * (desktop) for half a second to open it; while it is open the game sees no buttons.
 * The image is drawn in software (stb_easy_font) and composited by the backend over the presented
 * picture, scaled to the whole screen. Everything here runs on the render thread (which also pumps
 * input), except overlay_set_fps. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OVERLAY_W 640
#define OVERLAY_H 360

enum { OV_UP = 1, OV_DOWN = 2, OV_LEFT = 4, OV_RIGHT = 8, OV_ACCEPT = 16, OV_BACK = 32, OV_MENU = 64 };

/* each input poll, with the OV_* buttons held: 1 while the game must see a neutral controller */
int overlay_input(unsigned held);
/* the overlay image (OVERLAY_W x OVERLAY_H RGBA, straight alpha) or NULL when there is nothing to
 * show; *version changes whenever the pixels do */
const uint8_t* overlay_image(unsigned* version);
void overlay_set_fps(float fps);  /* any thread */
/* the menu's "Save screenshot": 1 once after it was chosen (the backend saves the next picture) */
int overlay_take_screenshot_request(void);

#ifdef __cplusplus
}
#endif
