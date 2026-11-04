// Vulkan backend: a solid colour (EFB clears that keep some channels: the colour write mask does the rest)
layout(location = 0) in vec2 v_uv;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; } pc;
layout(location = 0) out vec4 o_color;
void main() { o_color = pc.color; }
