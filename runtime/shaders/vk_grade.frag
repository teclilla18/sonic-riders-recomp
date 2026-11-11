// Vulkan backend (render_vk.c): colour grading of the 3D picture (as render_gl.c's composite).
// color = exposure, contrast, saturation, warmth.
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
layout(location = 0) out vec4 o_color;
void main() {
  vec3 c = texture(u_tex, v_uv).rgb;
  vec4 g = pc.color;
  c *= g.x;
  c = (c - 0.5) * g.y + 0.5;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(l), c, g.z);
  c *= vec3(1.0 + g.w, 1.0, 1.0 - g.w);
  o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
