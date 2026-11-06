// Vulkan backend: one touch control (platform/android/touch_android.c) on a square quad, drawn from
// distance functions so it stays sharp at any screen resolution. Premultiplied alpha out.
// shape.x: 0 button, 1 stick base, 2 stick knob; shape.y: icon (1 jump, 2 boost, 3 drift, 4 tornado,
// 5 pause (the word START), 6 menu); shape.z: 1 while pressed. color: tint (rgb) and overall opacity (a).
// Shared-GLSL rules apply (no runtime index into a vector or array).
layout(location = 0) in vec2 v_uv;
layout(push_constant) uniform PC { vec4 rect; vec4 uv; vec4 color; vec4 shape; vec4 rot; } pc;
layout(location = 0) out vec4 o_color;

float seg(vec2 p, vec2 a, vec2 b) {  // distance to a line segment
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h);
}

float icon(vec2 p, float id) {  // distance to the icon's strokes (y grows downwards)
  float d = 1e3;
  if (id < 1.5) {         // jump: an arrow up
    d = min(d, seg(p, vec2(0.0, 0.38), vec2(0.0, -0.36)));
    d = min(d, seg(p, vec2(-0.30, -0.06), vec2(0.0, -0.38)));
    d = min(d, seg(p, vec2(0.30, -0.06), vec2(0.0, -0.38)));
  } else if (id < 2.5) {  // boost: a double chevron forwards
    d = min(d, seg(p, vec2(-0.42, -0.30), vec2(-0.08, 0.0)));
    d = min(d, seg(p, vec2(-0.08, 0.0), vec2(-0.42, 0.30)));
    d = min(d, seg(p, vec2(0.02, -0.30), vec2(0.36, 0.0)));
    d = min(d, seg(p, vec2(0.36, 0.0), vec2(0.02, 0.30)));
  } else if (id < 3.5) {  // drift: a curved arrow
    float r = length(p - vec2(0.0, 0.12));
    float arc = abs(r - 0.36);
    if (p.y > 0.14 && p.x < 0.0) arc = 1e3;  // the arc's lower left quarter is open
    d = min(d, arc);
    d = min(d, seg(p, vec2(-0.36, 0.14), vec2(-0.54, -0.04)));
    d = min(d, seg(p, vec2(-0.36, 0.14), vec2(-0.16, -0.02)));
  } else if (id < 4.5) {  // tornado: a funnel of three lines
    d = min(d, seg(p, vec2(-0.40, -0.30), vec2(0.40, -0.30)));
    d = min(d, seg(p, vec2(-0.24, 0.0), vec2(0.28, 0.0)));
    d = min(d, seg(p, vec2(-0.06, 0.30), vec2(0.16, 0.30)));
  } else if (id < 5.5) {  // pause: the word START, centred (block letters 0.17 wide, 0.30 high)
    const float w = 0.17, h = 0.15;
    float x = -0.535;      // S
    d = min(d, seg(p, vec2(x + w, -h), vec2(x, -h)));
    d = min(d, seg(p, vec2(x, -h), vec2(x, 0.0)));
    d = min(d, seg(p, vec2(x, 0.0), vec2(x + w, 0.0)));
    d = min(d, seg(p, vec2(x + w, 0.0), vec2(x + w, h)));
    d = min(d, seg(p, vec2(x + w, h), vec2(x, h)));
    x += 0.225;            // T
    d = min(d, seg(p, vec2(x, -h), vec2(x + w, -h)));
    d = min(d, seg(p, vec2(x + 0.5 * w, -h), vec2(x + 0.5 * w, h)));
    x += 0.225;            // A
    d = min(d, seg(p, vec2(x, h), vec2(x + 0.5 * w, -h)));
    d = min(d, seg(p, vec2(x + 0.5 * w, -h), vec2(x + w, h)));
    d = min(d, seg(p, vec2(x + 0.22 * w, 0.04), vec2(x + 0.78 * w, 0.04)));
    x += 0.225;            // R
    d = min(d, seg(p, vec2(x, h), vec2(x, -h)));
    d = min(d, seg(p, vec2(x, -h), vec2(x + w, -h)));
    d = min(d, seg(p, vec2(x + w, -h), vec2(x + w, 0.0)));
    d = min(d, seg(p, vec2(x + w, 0.0), vec2(x, 0.0)));
    d = min(d, seg(p, vec2(x + 0.35 * w, 0.0), vec2(x + w, h)));
    x += 0.225;            // T
    d = min(d, seg(p, vec2(x, -h), vec2(x + w, -h)));
    d = min(d, seg(p, vec2(x + 0.5 * w, -h), vec2(x + 0.5 * w, h)));
    return d - 0.032;      // thinner strokes for letters
  } else {                // menu: three lines
    d = min(d, seg(p, vec2(-0.36, -0.28), vec2(0.36, -0.28)));
    d = min(d, seg(p, vec2(-0.36, 0.0), vec2(0.36, 0.0)));
    d = min(d, seg(p, vec2(-0.36, 0.28), vec2(0.36, 0.28)));
  }
  return d - 0.075;  // stroke half-width
}

void main() {
  vec2 p = v_uv * 2.0 - 1.0;
  float r = length(p);
  float aa = max(fwidth(r), 1e-4) * 1.5;
  float pressed = pc.shape.z;
  float kind = pc.shape.x;
  vec3 tint = pc.color.rgb;
  float fill_a, ring_a, icon_a = 0.0;
  if (kind < 0.5) {         // a button: a dark disc, a light rim, the icon
    fill_a = (1.0 - smoothstep(0.90 - aa, 0.90 + aa, r)) * mix(0.30, 0.62, pressed);
    ring_a = 1.0 - smoothstep(0.0, aa, abs(r - 0.88) - 0.045);
    icon_a = 1.0 - smoothstep(-aa, aa, icon(p, pc.shape.y));
  } else if (kind < 1.5) {  // the stick's base: a faint disc and a ring
    fill_a = (1.0 - smoothstep(0.92 - aa, 0.92 + aa, r)) * 0.16;
    ring_a = (1.0 - smoothstep(0.0, aa, abs(r - 0.90) - 0.025)) * 0.8;
  } else {                  // the stick's knob
    fill_a = (1.0 - smoothstep(0.88 - aa, 0.88 + aa, r)) * 0.55;
    ring_a = 1.0 - smoothstep(0.0, aa, abs(r - 0.86) - 0.05);
  }
  vec3 fill_c = mix(vec3(0.02, 0.05, 0.16), tint * 0.55, pressed);
  vec3 line_c = mix(tint, vec3(1.0), 0.35 + 0.65 * pressed);
  // front to back: icon over rim over fill (premultiplied)
  vec4 c = vec4(fill_c * fill_a, fill_a);
  c = vec4(line_c * ring_a, ring_a) + c * (1.0 - ring_a);
  c = vec4(vec3(1.0) * icon_a, icon_a) + c * (1.0 - icon_a);
  o_color = c * pc.color.a;
}
