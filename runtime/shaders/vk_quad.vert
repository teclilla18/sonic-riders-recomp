// Vulkan backend (render_vk.c): one screen-space quad per draw, placed by push constants: the presented
// picture, the overlay image, masked clears and the touch controls. Triangle strip of 4 vertices.
layout(push_constant) uniform PC {
  vec4 rect;   // x0, y0, x1, y1 in clip space (+y down: Vulkan)
  vec4 uv;     // u0, v0, u1, v1
  vec4 color;  // fill colour / tint
  vec4 shape;  // touch controls: kind, icon, pressed, aspect (vk_touch.frag)
} pc;
layout(location = 0) out vec2 v_uv;
void main() {
  bool r = (gl_VertexIndex & 1) != 0, b = (gl_VertexIndex & 2) != 0;
  v_uv = vec2(r ? pc.uv.z : pc.uv.x, b ? pc.uv.w : pc.uv.y);
  gl_Position = vec4(r ? pc.rect.z : pc.rect.x, b ? pc.rect.w : pc.rect.y, 0.0, 1.0);
}
