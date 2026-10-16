// overlay (settings menu, FPS counter): a full-screen quad from gl_VertexID, drawn as a triangle strip
VARY(0) out vec2 v_uv;
void main() {
  vec2 p = vec2((gl_VertexID & 1) != 0 ? 1.0 : -1.0, (gl_VertexID & 2) != 0 ? -1.0 : 1.0);
  v_uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  gl_Position = vec4(p, 0.0, 1.0);  // clip +y is the top on both backends here (no EFB involved)
}
