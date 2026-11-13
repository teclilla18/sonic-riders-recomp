// Vulkan (render_vk.c) and deko3d (render_deko.c, QUAD_DK): bloom blur, 9-tap Gaussian in 5 linear fetches (as render_gl.c).
// color.xy = one texel along the blur direction in uv (0: a plain linear copy), color.z = tint (the bloom's
// strength when it is drawn onto the EFB with a screen blend, else 1), color.w: 0 the 9-tap blur, 1 a plain copy with
// one texture read (the 9 taps at one place read the same texel five times), 2 the 9-tap blur applied twice in one
// pass (their 17-tap convolution in 9 linear fetches), shape.zw = the largest uv of the used part
// (targets are allocated at the largest size and used from the top left).
layout(location = 0) in vec2 v_uv;
#ifdef QUAD_DK  // deko3d (render_deko.c): a uniform buffer at binding 3, beside the GX shaders' 0-2
layout(binding = 0) uniform sampler2D u_tex;
layout(std140, binding = 3) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#else
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#endif
layout(location = 0) out vec4 o_color;
vec3 tap(vec2 p) { return texture(u_tex, clamp(p, vec2(0.0), pc.shape.zw)).rgb; }
void main() {
  vec3 c = tap(v_uv);
  vec2 d = pc.color.xy;
  if (pc.color.w <= 0.5) {
    c *= 0.2270270;
    c += (tap(v_uv + d * 1.3846154) + tap(v_uv - d * 1.3846154)) * 0.3162162;
    c += (tap(v_uv + d * 3.2307692) + tap(v_uv - d * 3.2307692)) * 0.0702703;
  } else if (pc.color.w > 1.5) {
    c *= 0.1632286;
    c += (tap(v_uv + d * 1.4394780) + tap(v_uv - d * 1.4394780)) * 0.2686633;
    c += (tap(v_uv + d * 3.3558404) + tap(v_uv - d * 3.3558404)) * 0.1213806;
    c += (tap(v_uv + d * 5.2608213) + tap(v_uv - d * 5.2608213)) * 0.0263258;
    c += (tap(v_uv + d * 7.1304346) + tap(v_uv - d * 7.1304346)) * 0.0020161;
  }
  o_color = vec4(c * pc.color.z, 1.0);
}
