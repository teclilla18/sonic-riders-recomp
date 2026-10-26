/* Dear ImGui draw data drawn in software into an RGBA image (menu_raster.cpp). */
#pragma once
#include <stdint.h>

struct ImDrawData;
/* answers ImGui's texture requests (on ImGui's thread, while no menu_raster runs) */
void menu_raster_textures(ImDrawData* dd);
/* draws over px (w x h RGBA, premultiplied alpha; the caller clears it); reads only dd and the pixels of
 * the textures it uses, so it can run on another thread on a copy of the draw lists */
void menu_raster(const ImDrawData* dd, uint8_t* px, int w, int h);
