/* Dear ImGui's draw data drawn in software into the overlay image (premultiplied-alpha RGBA,
 * overlay.h), so both renderers keep compositing one image and need no ImGui pipeline of their own.
 * ImGui's output is mostly axis-aligned rectangles (glyphs, frames, backgrounds: drawn as spans with
 * integer blending) plus the thin triangles of rounded corners and anti-aliased edges (barycentric,
 * top-left fill rule so shared edges are drawn once). Textures are ImGui's own (the font atlas):
 * ImGuiBackendFlags_RendererHasTextures, read straight from ImTextureData::Pixels. The menu turns
 * off textured anti-aliased lines (style.AntiAliasedLinesUseTex), which need filtered sampling. */
#include <math.h>
#include <string.h>

#include "imgui.h"
#include "menu_raster.h"

namespace {

inline uint32_t div255(uint32_t x) { return (x + 1 + (x >> 8)) >> 8; }

/* premultiplied source (r, g, b <= a) over the destination */
inline void blend(uint8_t* d, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    if (a == 0) return;
    if (a == 255 || (d[3] == 0 && (d[0] | d[1] | d[2]) == 0)) {  /* opaque, or over nothing yet */
        d[0] = (uint8_t)r;
        d[1] = (uint8_t)g;
        d[2] = (uint8_t)b;
        d[3] = (uint8_t)a;
        return;
    }
    uint32_t inv = 255 - a;
    d[0] = (uint8_t)(r + div255(d[0] * inv));
    d[1] = (uint8_t)(g + div255(d[1] * inv));
    d[2] = (uint8_t)(b + div255(d[2] * inv));
    d[3] = (uint8_t)(a + div255(d[3] * inv));
}

struct Rgba {
    uint32_t r, g, b, a;
};

inline Rgba unpack(ImU32 c) {
    return {(c >> IM_COL32_R_SHIFT) & 0xFF, (c >> IM_COL32_G_SHIFT) & 0xFF, (c >> IM_COL32_B_SHIFT) & 0xFF,
            (c >> IM_COL32_A_SHIFT) & 0xFF};
}

/* straight vertex colour x straight texel -> premultiplied */
inline Rgba modulate(const Rgba& c, const uint8_t* t, bool alpha8) {
    if (!t) return {div255(c.r * c.a), div255(c.g * c.a), div255(c.b * c.a), c.a};
    uint32_t a = div255(c.a * (alpha8 ? t[0] : t[3]));
    if (alpha8) return {div255(c.r * a), div255(c.g * a), div255(c.b * a), a};
    return {div255(div255(c.r * t[0]) * a), div255(div255(c.g * t[1]) * a), div255(div255(c.b * t[2]) * a), a};
}

struct Tex {
    const ImTextureData* t;
    bool a8;
    inline const uint8_t* texel(float u, float v) const {
        if (!t || !t->Pixels) return nullptr;
        int x = (int)(u * t->Width), y = (int)(v * t->Height);
        x = x < 0 ? 0 : x >= t->Width ? t->Width - 1 : x;
        y = y < 0 ? 0 : y >= t->Height ? t->Height - 1 : y;
        return (const uint8_t*)t->Pixels + ((size_t)y * t->Width + x) * t->BytesPerPixel;
    }
};

struct Clip {
    int x0, y0, x1, y1;
};

struct Target {
    uint8_t* px;
    int w, h;
};

/* an axis-aligned rectangle with one colour and a linear texture mapping */
void fill_rect(const Target& T, const Clip& c, const Tex& tex, const ImDrawVert& a, const ImDrawVert& b) {
    float x0 = fminf(a.pos.x, b.pos.x), x1 = fmaxf(a.pos.x, b.pos.x);
    float y0 = fminf(a.pos.y, b.pos.y), y1 = fmaxf(a.pos.y, b.pos.y);
    int ix0 = (int)ceilf(x0 - 0.5f), ix1 = (int)ceilf(x1 - 0.5f);
    int iy0 = (int)ceilf(y0 - 0.5f), iy1 = (int)ceilf(y1 - 0.5f);
    if (ix0 < c.x0) ix0 = c.x0;
    if (iy0 < c.y0) iy0 = c.y0;
    if (ix1 > c.x1) ix1 = c.x1;
    if (iy1 > c.y1) iy1 = c.y1;
    if (ix0 >= ix1 || iy0 >= iy1) return;
    const Rgba col = unpack(a.col);
    const float u0 = a.pos.x <= b.pos.x ? a.uv.x : b.uv.x, u1 = a.pos.x <= b.pos.x ? b.uv.x : a.uv.x;
    const float v0 = a.pos.y <= b.pos.y ? a.uv.y : b.uv.y, v1 = a.pos.y <= b.pos.y ? b.uv.y : a.uv.y;
    if (u0 == u1 && v0 == v1) {  /* a solid fill (ImGui samples its white texel) */
        const Rgba p = modulate(col, tex.texel(u0, v0), tex.a8);
        if (p.a == 0) return;
        const uint32_t word = p.r | p.g << 8 | p.b << 16 | p.a << 24;
        for (int y = iy0; y < iy1; y++) {
            uint8_t* row = T.px + ((size_t)y * T.w + ix0) * 4;
            if (p.a == 255) {
                for (int x = ix0; x < ix1; x++, row += 4) memcpy(row, &word, 4);
            } else {
                for (int x = ix0; x < ix1; x++, row += 4) blend(row, p.r, p.g, p.b, p.a);
            }
        }
        return;
    }
    const float du = x1 > x0 ? (u1 - u0) / (x1 - x0) : 0.0f, dv = y1 > y0 ? (v1 - v0) / (y1 - y0) : 0.0f;
    for (int y = iy0; y < iy1; y++) {
        const float v = v0 + dv * (y + 0.5f - y0);
        uint8_t* row = T.px + ((size_t)y * T.w + ix0) * 4;
        float u = u0 + du * (ix0 + 0.5f - x0);
        for (int x = ix0; x < ix1; x++, row += 4, u += du) {
            const Rgba p = modulate(col, tex.texel(u, v), tex.a8);
            blend(row, p.r, p.g, p.b, p.a);
        }
    }
}

/* edge function, and whether a pixel exactly on the edge belongs to this triangle (top-left rule for
 * a triangle with positive area in y-down screen space) */
struct Edge {
    float ax, ay, dx, dy;
    bool tl;
    Edge(const ImVec2& a, const ImVec2& b) : ax(a.x), ay(a.y), dx(b.x - a.x), dy(b.y - a.y) { tl = dy < 0 || (dy == 0 && dx > 0); }
    inline float at(float px, float py) const { return dx * (py - ay) - dy * (px - ax); }
    inline bool inside(float e) const { return e > 0 || (e == 0 && tl); }
};

void fill_tri(const Target& T, const Clip& c, const Tex& tex, const ImDrawVert* A, const ImDrawVert* B, const ImDrawVert* C) {
    float area = (B->pos.x - A->pos.x) * (C->pos.y - A->pos.y) - (B->pos.y - A->pos.y) * (C->pos.x - A->pos.x);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0) {
        const ImDrawVert* t = B;
        B = C;
        C = t;
        area = -area;
    }
    int ix0 = (int)floorf(fminf(A->pos.x, fminf(B->pos.x, C->pos.x)));
    int ix1 = (int)ceilf(fmaxf(A->pos.x, fmaxf(B->pos.x, C->pos.x)));
    int iy0 = (int)floorf(fminf(A->pos.y, fminf(B->pos.y, C->pos.y)));
    int iy1 = (int)ceilf(fmaxf(A->pos.y, fmaxf(B->pos.y, C->pos.y)));
    if (ix0 < c.x0) ix0 = c.x0;
    if (iy0 < c.y0) iy0 = c.y0;
    if (ix1 > c.x1) ix1 = c.x1;
    if (iy1 > c.y1) iy1 = c.y1;
    if (ix0 >= ix1 || iy0 >= iy1) return;
    const Edge e0(B->pos, C->pos), e1(C->pos, A->pos), e2(A->pos, B->pos);
    const Rgba ca = unpack(A->col), cb = unpack(B->col), cc = unpack(C->col);
    const bool flat_col = A->col == B->col && A->col == C->col;
    const bool flat_uv = A->uv.x == B->uv.x && A->uv.x == C->uv.x && A->uv.y == B->uv.y && A->uv.y == C->uv.y;
    const float inv = 1.0f / area;
    Rgba fp = {0, 0, 0, 0};
    if (flat_col && flat_uv) fp = modulate(ca, tex.texel(A->uv.x, A->uv.y), tex.a8);
    const Edge* edges[3] = {&e0, &e1, &e2};
    for (int y = iy0; y < iy1; y++) {
        const float py = y + 0.5f;
        uint8_t* row = T.px + ((size_t)y * T.w) * 4;
        /* this row's span: each edge function is linear in x (e = k - dy * px), so it bounds px on one
         * side; scanning only the span keeps large triangles (rounded rectangles are fans) cheap */
        float lo = (float)ix0, hi = (float)ix1;
        for (const Edge* e : edges) {
            const float k = e->dx * (py - e->ay) + e->dy * e->ax;  /* e(px) = k - dy * px */
            if (e->dy > 0) hi = fminf(hi, k / e->dy + 0.5f + 1.0f);   /* e >= 0  <=>  px <= k / dy */
            else if (e->dy < 0) lo = fmaxf(lo, k / e->dy - 0.5f - 1.0f);  /* px >= k / dy */
            else if (k < 0 || (k == 0 && !e->tl)) hi = lo;  /* a horizontal edge with the row outside */
        }
        int sx0 = (int)floorf(lo), sx1 = (int)ceilf(hi);
        if (sx0 < ix0) sx0 = ix0;
        if (sx1 > ix1) sx1 = ix1;
        for (int x = sx0; x < sx1; x++) {
            const float px = x + 0.5f;
            const float w0 = e0.at(px, py), w1 = e1.at(px, py), w2 = e2.at(px, py);
            if (!e0.inside(w0) || !e1.inside(w1) || !e2.inside(w2)) continue;
            if (flat_col && flat_uv) {
                blend(row + x * 4, fp.r, fp.g, fp.b, fp.a);
                continue;
            }
            const float b0 = w0 * inv, b1 = w1 * inv, b2 = 1.0f - b0 - b1;
            Rgba col = flat_col ? ca
                                : Rgba{(uint32_t)(b0 * ca.r + b1 * cb.r + b2 * cc.r + 0.5f), (uint32_t)(b0 * ca.g + b1 * cb.g + b2 * cc.g + 0.5f),
                                       (uint32_t)(b0 * ca.b + b1 * cb.b + b2 * cc.b + 0.5f), (uint32_t)(b0 * ca.a + b1 * cb.a + b2 * cc.a + 0.5f)};
            const uint8_t* t = flat_uv ? tex.texel(A->uv.x, A->uv.y)
                                       : tex.texel(b0 * A->uv.x + b1 * B->uv.x + b2 * C->uv.x, b0 * A->uv.y + b1 * B->uv.y + b2 * C->uv.y);
            const Rgba p = modulate(col, t, tex.a8);
            blend(row + x * 4, p.r, p.g, p.b, p.a);
        }
    }
}

/* two triangles (i, i+1, i+2), (i, i+2, i+3) of ImGui's PrimRect / PrimRectUV: an axis-aligned
 * rectangle of one colour */
bool is_rect(const ImDrawVert* v, const ImDrawIdx* ix, unsigned n, unsigned i) {
    if (i + 6 > n) return false;
    const ImDrawIdx a = ix[i], b = ix[i + 1], c = ix[i + 2];
    if (b != (ImDrawIdx)(a + 1) || c != (ImDrawIdx)(a + 2) || ix[i + 3] != a || ix[i + 4] != c || ix[i + 5] != (ImDrawIdx)(a + 3)) return false;
    const ImDrawVert &p0 = v[a], &p1 = v[a + 1], &p2 = v[a + 2], &p3 = v[a + 3];
    if (p0.col != p1.col || p0.col != p2.col || p0.col != p3.col) return false;
    /* p0 top-left, p1 top-right, p2 bottom-right, p3 bottom-left */
    return p0.pos.y == p1.pos.y && p2.pos.y == p3.pos.y && p0.pos.x == p3.pos.x && p1.pos.x == p2.pos.x &&
           p0.uv.y == p1.uv.y && p2.uv.y == p3.uv.y && p0.uv.x == p3.uv.x && p1.uv.x == p2.uv.x;
}

}  // namespace

void menu_raster_textures(ImDrawData* dd) {
    /* ImGui 1.92's protocol, kept on the CPU side (the pixels are read in place) */
    if (dd->Textures) {
        for (ImTextureData* t : *dd->Textures) {
            if (t->Status == ImTextureStatus_WantCreate) {
                t->SetTexID((ImTextureID)(uintptr_t)t);
                t->SetStatus(ImTextureStatus_OK);
            } else if (t->Status == ImTextureStatus_WantUpdates) {
                t->SetStatus(ImTextureStatus_OK);
            } else if (t->Status == ImTextureStatus_WantDestroy && t->UnusedFrames > 0) {
                t->SetTexID(ImTextureID_Invalid);
                t->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
}

void menu_raster(const ImDrawData* dd, uint8_t* px, int w, int h) {
    Target T{px, w, h};
    const ImVec2 off = dd->DisplayPos;
    for (const ImDrawList* list : dd->CmdLists) {
        const ImDrawVert* vtx = list->VtxBuffer.Data;
        const ImDrawIdx* idx = list->IdxBuffer.Data;
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) continue;
            Clip c{(int)fmaxf(0.0f, cmd.ClipRect.x - off.x), (int)fmaxf(0.0f, cmd.ClipRect.y - off.y),
                   (int)fminf((float)w, cmd.ClipRect.z - off.x), (int)fminf((float)h, cmd.ClipRect.w - off.y)};
            if (c.x0 >= c.x1 || c.y0 >= c.y1) continue;
            const ImTextureData* td = (const ImTextureData*)(uintptr_t)cmd.GetTexID();
            Tex tex{td, td && td->Format == ImTextureFormat_Alpha8};
            const ImDrawVert* v = vtx + cmd.VtxOffset;
            const ImDrawIdx* ix = idx + cmd.IdxOffset;
            unsigned n = cmd.ElemCount;
            for (unsigned i = 0; i + 3 <= n;) {
                if (is_rect(v, ix, n, i)) {
                    fill_rect(T, c, tex, v[ix[i]], v[ix[i + 2]]);
                    i += 6;
                } else {
                    fill_tri(T, c, tex, &v[ix[i]], &v[ix[i + 1]], &v[ix[i + 2]]);
                    i += 3;
                }
            }
        }
    }
}
