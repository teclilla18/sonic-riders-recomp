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
// Vulkan (render_vk.c) and deko3d (render_deko.c, QUAD_DK): FSR 1 RCAS, the scaled picture sharpened onto the screen. uv runs over the
// source's pixels; color.x = sharpness (AMD's con.x: 1 = the most); shape.zw = the last texel.
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
  vec2 q = clamp(vec2(p), vec2(0.0), pc.shape.zw) + 0.5;
  return textureLod(u_tex, q / vec2(textureSize(u_tex, 0)), 0.0).rgb;
}
void main() {
  ivec2 p = ivec2(floor(v_uv));
  vec3 b = tx(p + ivec2(0, -1)), d = tx(p + ivec2(-1, 0)), e = tx(p), f = tx(p + ivec2(1, 0)), h = tx(p + ivec2(0, 1));
  vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
  vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1e-6));
  vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1e-6));
  vec3 lobe3 = max(-hitMin, hitMax);
  float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * pc.color.x;
  o_color = vec4((lobe * (b + d + h + f) + e) / (4.0 * lobe + 1.0), 1.0);
}
