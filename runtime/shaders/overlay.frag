// overlay: the image (row 0 at the top), premultiplied alpha (menu_raster.cpp)
VARY(0) in vec2 v_uv;
SAMPLER(0) uniform sampler2D u_ov;
layout(location = 0) out vec4 o_color;
void main() { o_color = texture(u_ov, v_uv); }
