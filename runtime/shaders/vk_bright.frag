// Vulkan (render_vk.c) and deko3d (render_deko.c, QUAD_DK): bloom bright pass. 16 linear taps (4x4) over this pixel's footprint in the EFB
// (no mip levels there; render_gl.c reads a mip level instead), then the bright part of their average.
// color.xy = tap step in uv, shape.zw = the largest uv of the EFB's used part.
layout(location = 0) in vec2 v_uv;
#ifdef QUAD_DK  // deko3d (render_deko.c): a uniform buffer at binding 3, beside the GX shaders' 0-2
layout(binding = 0) uniform sampler2D u_tex;
layout(std140, binding = 3) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#else
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#endif
layout(location = 0) out vec4 o_color;
void main() {
  vec3 c = vec3(0.0);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++)
      c += texture(u_tex, min(v_uv + (vec2(float(x), float(y)) - 1.5) * pc.color.xy, pc.shape.zw)).rgb;
  c *= 1.0 / 16.0;
  float k = clamp((max(c.r, max(c.g, c.b)) - 0.5) / 0.5, 0.0, 1.0);
  o_color = vec4(c * k * k, 1.0);
}
