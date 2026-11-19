// Shared Windows installer geometry, in physical pixels after applying monitor DPI.
#pragma once
#include <algorithm>
namespace installer_layout {
struct Layout {
    float side, banner, bar, pad, content_x, content_w;
};
inline Layout measure(float w, float h, float dpi) {
    float side = 220 * dpi, pad = 24 * dpi;
    float available = std::max(1.0f, w - side - 1 - 2 * pad);
    float width = std::min(available, 1400 * dpi);
    return {side, std::clamp(h * 0.27f, 140 * dpi, 220 * dpi), 112 * dpi,
            pad, side + 1 + pad + (available - width) * 0.5f, width};
}
inline int columns(float width, float dpi) { return width >= 900 * dpi ? 2 : 1; }
struct ImageRect { float x, y, w, h; };
inline ImageRect contain(float x, float y, float w, float h, float iw, float ih) {
    float scale = std::min(w / iw, h / ih);
    float fw = iw * scale, fh = ih * scale;
    return {x + (w - fw) * 0.5f, y + (h - fh) * 0.5f, fw, fh};
}
} // namespace installer_layout
