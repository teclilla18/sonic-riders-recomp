// deko3d (render_deko.c, QUAD_DK): both bloom levels onto the EFB in one pass, drawn with the screen blend
// (1 - (1 - dst) * (1 - src)). Two screen blends in a row equal one with src = a + b - a * b, so this draws
// the same picture as the two separate passes did, with one texture read per level instead of five each.
// v_uv = level 0's uv; color.xy = level 1's uv per level 0 uv, color.z = the bloom's strength;
// shape.xy / shape.zw = the largest uv of level 1's / level 0's used part.
layout(location = 0) in vec2 v_uv;
#ifdef QUAD_DK
layout(binding = 0) uniform sampler2D u_tex;
layout(binding = 1) uniform sampler2D u_tex1;
layout(std140, binding = 3) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#else
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(set = 0, binding = 1) uniform sampler2D u_tex1;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
#endif
layout(location = 0) out vec4 o_color;
void main() {
  vec3 a = texture(u_tex, min(v_uv, pc.shape.zw)).rgb * pc.color.z;
  vec3 b = texture(u_tex1, min(v_uv * pc.color.xy, pc.shape.xy)).rgb * pc.color.z;
  o_color = vec4(a + b - a * b, 1.0);
}
