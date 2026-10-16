// GX ubershader (part of the shared GLSL; see runtime/shaders/README.md for the prelude macros)
layout(std140 BIND_UBO) uniform GX { uvec4 bp[64]; uvec4 xfr[32]; uvec4 konst[2]; };
layout(std140 BIND_UBO1) uniform GXT { uvec4 xtab[8]; };  // the XF memory chunk table (GxXTab)
// XF memory: chunks of 64 words in a per-frame pool (XFM_WORD(i) reads pool word i: a storage buffer on
// deko3d, a texture buffer on GL), chunk c of this state at pool entry xtab[c] (gx_render.h)
// Components are picked with explicit selects: uam (deko3d) miscompiles a runtime component index
// into a uniform-buffer vector (v[i & 3] returned wrong values for i & 3 != 0 in the vertex stage).
uint pick(uvec4 v, int c) { return c == 0 ? v.x : c == 1 ? v.y : c == 2 ? v.z : v.w; }
uint BP(int i) { return pick(bp[i >> 2], i & 3); }
uint XR(int i) { return pick(xfr[i >> 2], i & 3); }
uint XMU(int i) { int c = i >> 6; return XFM_WORD(int(pick(xtab[c >> 2], c & 3)) * 64 + (i & 63)); }
float XM(int i) { return uintBitsToFloat(XMU(i)); }
uint KR(int i) { return pick(konst[i >> 2], i & 3); }
uint bits(uint v, int s, int n) { return (v >> uint(s)) & ((1u << uint(n)) - 1u); }
vec4 rgba8(uint c) { return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0; }
