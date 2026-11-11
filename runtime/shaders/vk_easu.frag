// AMD FidelityFX Super Resolution 1, ported from AMD's ffx_fsr1.h (v1.20210629, 32-bit paths) as in
// runtime/src/platform/host/fsr1_gl.h, under its MIT license:
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Vulkan (render_vk.c) and deko3d (render_deko.c, QUAD_DK): FSR 1 EASU, the EFB's shown part scaled up to the picture's size. The quad's uv
// runs over output pixels (0..size, centres at .5), so the screen's pre-rotation does not matter.
// color = AMD's con0 (input / output size, 0.5 * that - 0.5); shape = first and last texel of the source rect.
layout(location = 0) in vec2 v_uv;
#ifdef QUAD_DK  // deko3d (render_deko.c): a uniform buffer at binding 3, beside the GX shaders' 0-2
layout(binding = 0) uniform sampler2D u_tex;
layout(std140, binding = 3) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#else
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#endif
layout(location = 0) out vec4 o_color;
// a texel by float coordinates with the nearest sampler: on Switch hardware an integer clamp + texelFetch
// read only the first column (fine in Eden and on Vulkan)
vec3 tx(ivec2 p) {
  vec2 q = clamp(vec2(p), pc.shape.xy, pc.shape.zw) + 0.5;
  return textureLod(u_tex, q / vec2(textureSize(u_tex, 0)), 0.0).rgb;
}
float lum(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
void tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
  vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
  float d2 = min(v.x * v.x + v.y * v.y, clp);
  float wB = 2.0 / 5.0 * d2 - 1.0;
  float wA = lob * d2 - 1.0;
  wB *= wB; wA *= wA;
  wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
  float w = wB * wA;
  aC += c * w; aW += w;
}
void set(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
  float lenX = max(abs(lD - lC), abs(lC - lB));
  float dirX = lD - lB;
  dir.x += dirX * w;
  lenX = clamp(abs(dirX) / max(lenX, 1e-8), 0.0, 1.0);
  len += lenX * lenX * w;
  float lenY = max(abs(lE - lC), abs(lC - lA));
  float dirY = lE - lA;
  dir.y += dirY * w;
  lenY = clamp(abs(dirY) / max(lenY, 1e-8), 0.0, 1.0);
  len += lenY * lenY * w;
}
void main() {
  vec2 pp = (v_uv - 0.5) * pc.color.xy + pc.color.zw;
  vec2 fp = floor(pp);
  pp -= fp;
  ivec2 q = ivec2(fp) + ivec2(pc.shape.xy);
  //   b c
  // e f g h
  // i j k l
  //   n o      (f = q)
  vec3 b = tx(q + ivec2(0, -1)), c = tx(q + ivec2(1, -1));
  vec3 e = tx(q + ivec2(-1, 0)), f = tx(q), g = tx(q + ivec2(1, 0)), h = tx(q + ivec2(2, 0));
  vec3 i = tx(q + ivec2(-1, 1)), j = tx(q + ivec2(0, 1)), k = tx(q + ivec2(1, 1)), l = tx(q + ivec2(2, 1));
  vec3 n = tx(q + ivec2(0, 2)), oo = tx(q + ivec2(1, 2));
  float bL = lum(b), cL = lum(c), eL = lum(e), fL = lum(f), gL = lum(g), hL = lum(h);
  float iL = lum(i), jL = lum(j), kL = lum(k), lL = lum(l), nL = lum(n), oL = lum(oo);
  vec2 dir = vec2(0.0);
  float len = 0.0;
  set(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
  set(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
  set(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
  set(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
  float dirR = dir.x * dir.x + dir.y * dir.y;
  bool zro = dirR < 1.0 / 32768.0;
  dirR = zro ? 1.0 : inversesqrt(dirR);
  dir.x = zro ? 1.0 : dir.x;
  dir *= dirR;
  len = len * 0.5;
  len *= len;
  float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
  vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = 1.0 / lob;
  vec3 mn = min(min(f, g), min(j, k)), mx = max(max(f, g), max(j, k));
  vec3 aC = vec3(0.0);
  float aW = 0.0;
  tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
  tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
  tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
  tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
  tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
  tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
  tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
  tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
  tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
  tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
  tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, oo);
  tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
  o_color = vec4(min(mx, max(mn, aC / aW)), 1.0);
}
