// deko3d (render_deko.c, QUAD_DK): bloom and colour grading in one pass. u_tex = a copy of the EFB's picture,
// u_l0 / u_l1 = the two bloom levels. The bloom goes on with the screen blend's formula (1 - (1 - c) * (1 - s),
// both levels: s = a + b - a * b), then the grading of vk_grade.frag: the picture the bloom pass and the grading
// pass drew one after the other, in one full-screen pass instead of two.
// color = exposure, contrast, saturation, warmth; shape.xy / shape.zw = level 0's / level 1's uv per picture uv;
// ext0.xy / ext0.zw = the largest uv of level 0's / level 1's used part; ext1.x = the bloom's strength (0: none).
layout(location = 0) in vec2 v_uv;
layout(binding = 0) uniform sampler2D u_tex;
layout(binding = 1) uniform sampler2D u_l0;
layout(binding = 2) uniform sampler2D u_l1;
layout(std140, binding = 3) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; vec4 ext0; vec4 ext1; } pc;
layout(location = 0) out vec4 o_color;
void main() {
  vec3 c = texture(u_tex, v_uv).rgb;
  if (pc.ext1.x > 0.0) {
    vec3 a = texture(u_l0, min(v_uv * pc.shape.xy, pc.ext0.xy)).rgb * pc.ext1.x;
    vec3 b = texture(u_l1, min(v_uv * pc.shape.zw, pc.ext0.zw)).rgb * pc.ext1.x;
    vec3 s = a + b - a * b;
    c = c + s - c * s;
  }
  vec4 g = pc.color;
  c *= g.x;
  c = (c - 0.5) * g.y + 0.5;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(l), c, g.z);
  c *= vec3(1.0 + g.w, 1.0, 1.0 - g.w);
  o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
