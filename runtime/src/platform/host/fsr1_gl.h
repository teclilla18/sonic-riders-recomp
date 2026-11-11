/* AMD FidelityFX Super Resolution 1 (EASU upscaling, RCAS sharpening) as GLSL 3.30 fragment shaders for the
 * PC renderer's present pass (render_gl.c fsr_present). Ported from AMD's ffx_fsr1.h (FidelityFX-FSR,
 * v1.20210629, 32-bit "F" paths) under its MIT license, below. Changes from AMD's code: texelFetch of the 12
 * EASU taps instead of textureGather (GL 3.3 has none), exact reciprocals with guards instead of the
 * approximations, the constants worked out in the shader, no RCAS denoise path.
 *
 * Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

/* EASU: u_tex (the picture, u_max = its size - 1) scaled to the viewport. u_con.xy = input / output size,
 * u_con.zw = 0.5 * u_con.xy - 0.5 (AMD's FsrEasuCon con0). Rows go up in both (GL order); the filter is
 * symmetric, so only the tap names follow AMD's (y down) picture. */
static const char k_fsr_easu[] =
    "out vec4 o;\n"
    "uniform sampler2D u_tex; uniform vec4 u_con; uniform ivec2 u_max;\n"
    "vec3 tx(ivec2 p) { return texelFetch(u_tex, clamp(p, ivec2(0), u_max), 0).rgb; }\n"
    "float lum(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    "void tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {\n"
    "  vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;\n"
    "  float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
    "  float wB = 2.0 / 5.0 * d2 - 1.0;\n"
    "  float wA = lob * d2 - 1.0;\n"
    "  wB *= wB; wA *= wA;\n"
    "  wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);\n"
    "  float w = wB * wA;\n"
    "  aC += c * w; aW += w;\n"
    "}\n"
    "void set(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {\n"
    "  float lenX = max(abs(lD - lC), abs(lC - lB));\n"
    "  float dirX = lD - lB;\n"
    "  dir.x += dirX * w;\n"
    "  lenX = clamp(abs(dirX) / max(lenX, 1e-8), 0.0, 1.0);\n"
    "  len += lenX * lenX * w;\n"
    "  float lenY = max(abs(lE - lC), abs(lC - lA));\n"
    "  float dirY = lE - lA;\n"
    "  dir.y += dirY * w;\n"
    "  lenY = clamp(abs(dirY) / max(lenY, 1e-8), 0.0, 1.0);\n"
    "  len += lenY * lenY * w;\n"
    "}\n"
    "void main() {\n"
    "  vec2 pp = (gl_FragCoord.xy - 0.5) * u_con.xy + u_con.zw;\n"
    "  vec2 fp = floor(pp);\n"
    "  pp -= fp;\n"
    "  ivec2 q = ivec2(fp);\n"
    /*      b c
     *    e f g h
     *    i j k l
     *      n o      (f = q) */
    "  vec3 b = tx(q + ivec2(0, -1)), c = tx(q + ivec2(1, -1));\n"
    "  vec3 e = tx(q + ivec2(-1, 0)), f = tx(q), g = tx(q + ivec2(1, 0)), h = tx(q + ivec2(2, 0));\n"
    "  vec3 i = tx(q + ivec2(-1, 1)), j = tx(q + ivec2(0, 1)), k = tx(q + ivec2(1, 1)), l = tx(q + ivec2(2, 1));\n"
    "  vec3 n = tx(q + ivec2(0, 2)), oo = tx(q + ivec2(1, 2));\n"
    "  float bL = lum(b), cL = lum(c), eL = lum(e), fL = lum(f), gL = lum(g), hL = lum(h);\n"
    "  float iL = lum(i), jL = lum(j), kL = lum(k), lL = lum(l), nL = lum(n), oL = lum(oo);\n"
    "  vec2 dir = vec2(0.0);\n"
    "  float len = 0.0;\n"
    "  set(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);\n"
    "  set(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);\n"
    "  set(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);\n"
    "  set(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);\n"
    "  float dirR = dir.x * dir.x + dir.y * dir.y;\n"
    "  bool zro = dirR < 1.0 / 32768.0;\n"
    "  dirR = zro ? 1.0 : inversesqrt(dirR);\n"
    "  dir.x = zro ? 1.0 : dir.x;\n"
    "  dir *= dirR;\n"
    "  len = len * 0.5;\n"
    "  len *= len;\n"
    "  float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));\n"
    "  vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;\n"
    "  float clp = 1.0 / lob;\n"
    "  vec3 mn = min(min(f, g), min(j, k)), mx = max(max(f, g), max(j, k));\n"
    "  vec3 aC = vec3(0.0);\n"
    "  float aW = 0.0;\n"
    "  tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "  tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "  tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "  tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "  tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "  tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "  tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "  tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "  tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "  tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "  tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, oo);\n"
    "  tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "  o = vec4(min(mx, max(mn, aC / aW)), 1.0);\n"
    "}\n";

/* RCAS: u_tex (same size as the viewport, whose corner is u_off) sharpened by u_sharp (AMD's con.x:
 * 1 = the most, each halving one stop less) */
static const char k_fsr_rcas[] =
    "out vec4 o;\n"
    "uniform sampler2D u_tex; uniform ivec2 u_off, u_max; uniform float u_sharp;\n"
    "vec3 tx(ivec2 p) { return texelFetch(u_tex, clamp(p, ivec2(0), u_max), 0).rgb; }\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy) - u_off;\n"
    "  vec3 b = tx(p + ivec2(0, -1)), d = tx(p + ivec2(-1, 0)), e = tx(p), f = tx(p + ivec2(1, 0)), h = tx(p + ivec2(0, 1));\n"
    "  vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));\n"
    "  vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1e-6));\n"
    "  vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1e-6));\n"
    "  vec3 lobe3 = max(-hitMin, hitMax);\n"
    "  float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobe3.r, max(lobe3.g, lobe3.b)), 0.0)) * u_sharp;\n"
    "  o = vec4((lobe * (b + d + h + f) + e) / (4.0 * lobe + 1.0), 1.0);\n"
    "}\n";
