/* Touch controls (touch_android.c): a floating stick on the left, jump / boost / drift / tornado on the
 * right, pause and menu buttons in the top corners. Everything runs on the render thread (SDL events,
 * input_pump, drawing). */
#pragma once
#include <SDL.h>

#include "runtime.h"

typedef struct TouchShape {
    float cx, cy, r;  /* window pixels */
    int kind;         /* 0 button, 1 stick base, 2 stick knob (vk_touch.frag) */
    int icon;         /* 1 jump, 2 boost, 3 drift, 4 tornado, 5 pause, 6 menu */
    int pressed;
    float alpha;
    float tint[3];
} TouchShape;

void touch_event(const SDL_Event* e, int win_w, int win_h);  /* finger events; a gamepad in use hides the controls */
void touch_apply(GcPad* p);                                  /* into controller port 1 (input_sdl.c) */
int touch_shapes(TouchShape* out, int max, int win_w, int win_h);  /* what to draw now (render_vk.c) */
void touch_set_overlay_rect(float x0, float y0, float x1, float y1);  /* where the menu image is on screen */
