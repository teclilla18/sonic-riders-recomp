/* Dear ImGui draw data drawn in software into an RGBA image (menu_raster.cpp). */
#pragma once
#include <stdint.h>

struct ImDrawData;
/* draws over px (w x h RGBA, straight alpha; the caller clears it) and handles ImGui's texture requests */
void menu_raster(ImDrawData* dd, uint8_t* px, int w, int h);
