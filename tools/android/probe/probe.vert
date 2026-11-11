#version 450
// Probe shader (tools/android/probe/probe.c): a tiny triangle placed by its uniform range.
layout(std140, set = 0, binding = 0) uniform U { vec4 off; vec4 col; };
layout(location = 0) out vec4 v_col;
void main() {
  vec2 p = vec2(gl_VertexIndex == 1 ? 0.02 : 0.0, gl_VertexIndex == 2 ? 0.02 : 0.0);
  gl_Position = vec4(p + off.xy, 0.0, 1.0);
  v_col = col;
}
