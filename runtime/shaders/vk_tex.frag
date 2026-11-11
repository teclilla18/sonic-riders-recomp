// Vulkan backend: a textured quad (the picture from the EFB; the overlay image, premultiplied alpha)
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
layout(location = 0) out vec4 o_color;
void main() { o_color = texture(u_tex, v_uv) * pc.color; }
