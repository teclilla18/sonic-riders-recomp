#version 450
layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 o_color;
void main() { o_color = v_col; }
