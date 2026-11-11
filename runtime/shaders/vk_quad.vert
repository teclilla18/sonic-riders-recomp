// Vulkan backend (render_vk.c): one screen-space quad per draw, placed by push constants: the presented
// picture, the overlay image, masked clears and the touch controls. Triangle strip of 4 vertices.
#ifdef QUAD_DK  // deko3d (render_deko.c): picture options' passes; a uniform buffer, clip +y up
#define gl_VertexIndex gl_VertexID
layout(std140, binding = 3) uniform PC {
#else
layout(push_constant) uniform PC {
#endif
  vec4 rect;   // x0, y0, x1, y1 in clip space (+y down: Vulkan)
  vec4 uv;     // u0, v0, u1, v1
  vec4 color;  // fill colour / tint
  vec4 shape;  // touch controls: kind, icon, pressed, aspect (vk_touch.frag)
  vec4 rot;    // 2x2 clip-space rotation (column-major): the swapchain's pre-rotation, else identity
} pc;
layout(location = 0) out vec2 v_uv;
void main() {
  bool r = (gl_VertexIndex & 1) != 0, b = (gl_VertexIndex & 2) != 0;
  v_uv = vec2(r ? pc.uv.z : pc.uv.x, b ? pc.uv.w : pc.uv.y);
  vec2 p = vec2(r ? pc.rect.z : pc.rect.x, b ? pc.rect.w : pc.rect.y);
  gl_Position = vec4(mat2(pc.rot.xy, pc.rot.zw) * p, 0.0, 1.0);
#ifdef QUAD_DK
  gl_Position.y = -gl_Position.y;
#endif
}
