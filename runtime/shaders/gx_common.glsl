// GX ubershader (part of the shared GLSL; see runtime/shaders/README.md for the prelude macros)
layout(std140 BIND_UBO) uniform GX { uvec4 bp[64]; uvec4 xfr[32]; uvec4 konst[2]; };
#ifdef XF_UBO
// deko3d: the XF memory a state's vertex shader reads, as one uniform buffer (render_deko.c xf_image):
// 0x000-0x0FF matrices, 0x400-0x47F normal matrices, 0x500-0x67F post matrices and lights, packed into
// 0x300 words. Uniform buffers go through the constant cache, much faster than a storage buffer when the
// vertices of a warp read the same words (the usual case: one matrix per draw).
layout(std140 BIND_UBO1) uniform GXF { uvec4 xf[192]; };
#else
#ifndef BIND_UBO_XT
#define BIND_UBO_XT BIND_UBO1
#endif
layout(std140 BIND_UBO_XT) uniform GXT { uvec4 xtab[8]; };  // the XF memory chunk table (GxXTab)
#endif
// XF memory: chunks of 64 words in a per-frame pool (XFM_WORD(i) reads pool word i: a storage buffer on
// deko3d, a texture buffer on GL), chunk c of this state at pool entry xtab[c] (gx_render.h)
// Components are picked with explicit selects: uam (deko3d) miscompiles a runtime component index
// into a uniform-buffer vector (v[i & 3] returned wrong values for i & 3 != 0 in the vertex stage).
uint pick(uvec4 v, int c) { return c == 0 ? v.x : c == 1 ? v.y : c == 2 ? v.z : v.w; }
uint BP(int i) { return pick(bp[i >> 2], i & 3); }
uint XR(int i) { return pick(xfr[i >> 2], i & 3); }
#ifdef XF_UBO
int xfi(int i) { return i < 0x400 ? i : i < 0x500 ? i - 0x300 : i - 0x380; }  // XF address -> xf[] word
uint XMU(int i) { int j = xfi(i); return pick(xf[j >> 2], j & 3); }
#else
uint XMU(int i) { int c = i >> 6; return XFM_WORD(int(pick(xtab[c >> 2], c & 3)) * 64 + (i & 63)); }
#endif
float XM(int i) { return uintBitsToFloat(XMU(i)); }
uint KR(int i) { return pick(konst[i >> 2], i & 3); }
uint bits(uint v, int s, int n) { return (v >> uint(s)) & ((1u << uint(n)) - 1u); }
vec4 rgba8(uint c) { return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0; }
