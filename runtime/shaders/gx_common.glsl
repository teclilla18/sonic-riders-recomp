// GX ubershader (part of the shared GLSL; see runtime/shaders/README.md for the prelude macros)
layout(std140 BIND_UBO) uniform GX { uvec4 bp[64]; uvec4 xfr[32]; uvec4 xfm[416]; uvec4 konst[2]; };
uint BP(int i) { return bp[i >> 2][i & 3]; }
uint XR(int i) { return xfr[i >> 2][i & 3]; }
float XM(int i) { return uintBitsToFloat(xfm[i >> 2][i & 3]); }
uint XMU(int i) { return xfm[i >> 2][i & 3]; }
uint KR(int i) { return konst[i >> 2][i & 3]; }
uint bits(uint v, int s, int n) { return (v >> uint(s)) & ((1u << uint(n)) - 1u); }
vec4 rgba8(uint c) { return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)) / 255.0; }
