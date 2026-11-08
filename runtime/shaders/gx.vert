// GX ubershader (part of the shared GLSL; see runtime/shaders/README.md for the prelude macros)
ATTR(0) in vec4 a_pos;
ATTR(1) in vec4 a_nrm;
ATTR(2) in vec4 a_col0;
ATTR(3) in vec4 a_col1;
ATTR(4) in vec4 a_tex01;
ATTR(5) in vec4 a_tex23;
ATTR(6) in vec4 a_tex45;
ATTR(7) in vec4 a_tex67;
ATTR(8) in vec4 a_tm0;
ATTR(9) in vec4 a_tm1;
VARY(0) out vec4 v_col0;
VARY(1) out vec4 v_col1;
VARY(2) out vec3 v_tex0;
 VARY(3) out vec3 v_tex1;
 VARY(4) out vec3 v_tex2;
 VARY(5) out vec3 v_tex3;

VARY(6) out vec3 v_tex4;
 VARY(7) out vec3 v_tex5;
 VARY(8) out vec3 v_tex6;
 VARY(9) out vec3 v_tex7;
#ifdef PERPIXEL_LIGHT
/* PC per-pixel lighting (gx.frag): what the lighting needs, interpolated */
out vec3 v_pv; out vec3 v_n; out vec4 v_vc0; out vec4 v_vc1;
#endif

#ifdef XF_UBO
vec4 row(int a) { return uintBitsToFloat(xf[xfi(a) >> 2]); }  /* a is a multiple of 4: one vector */
#else
vec4 row(int a) { return vec4(XM(a), XM(a + 1), XM(a + 2), XM(a + 3)); }
#endif
//@gx_light
vec3 texgen(int i, vec3 pos_raw, vec3 n, vec4 c0, vec4 c1, float tmtx) {
  uint info = XR(0x40 + i);
  uint src = bits(info, 7, 5), type = bits(info, 4, 3);
  vec4 s;
  if (src == 0u) s = vec4(pos_raw, 1.0);
  else if (src <= 4u) s = vec4(n, 1.0);
  else {
    int k = int(src) - 5;
    vec4 t = k == 0 ? vec4(a_tex01.xy, 1, 1) : k == 1 ? vec4(a_tex01.zw, 1, 1) : k == 2 ? vec4(a_tex23.xy, 1, 1) :
             k == 3 ? vec4(a_tex23.zw, 1, 1) : k == 4 ? vec4(a_tex45.xy, 1, 1) : k == 5 ? vec4(a_tex45.zw, 1, 1) :
             k == 6 ? vec4(a_tex67.xy, 1, 1) : vec4(a_tex67.zw, 1, 1);
    s = t;
  }
  if (bits(info, 2, 1) == 0u) s.z = 1.0;  /* AB11 */
  vec3 c;
  if (type == 2u) c = vec3(c0.rg, 1.0);
  else if (type == 3u) c = vec3(c1.rg, 1.0);
  else if (type == 1u) c = vec3(s.xy, 1.0);  /* emboss: approximated by the source coordinate */
  else {
    int a = int(tmtx) * 4;
    if (bits(info, 1, 1) != 0u) c = vec3(dot(row(a), s), dot(row(a + 4), s), dot(row(a + 8), s));
    else c = vec3(dot(row(a), s), dot(row(a + 4), s), 1.0);
    if (bits(XR(0x12), 0, 1) != 0u) {  /* dual texture transform: post matrix */
      uint post = XR(0x50 + i);
      if (bits(post, 8, 1) != 0u) c = normalize(c);
      int p = 0x500 + int(bits(post, 0, 6)) * 4;
      vec4 cc = vec4(c, 1.0);
      c = vec3(dot(row(p), cc), dot(row(p + 4), cc), dot(row(p + 8), cc));
    }
  }
  return c;
}
void main() {
  int pm = int(a_pos.w) * 4;
  vec4 p4 = vec4(a_pos.xyz, 1.0);
  vec3 pv = vec3(dot(row(pm), p4), dot(row(pm + 4), p4), dot(row(pm + 8), p4));
  int nm = 0x400 + (int(a_pos.w) & 31) * 3;
  vec3 n = vec3(dot(vec3(XM(nm), XM(nm + 1), XM(nm + 2)), a_nrm.xyz), dot(vec3(XM(nm + 3), XM(nm + 4), XM(nm + 5)), a_nrm.xyz),
                dot(vec3(XM(nm + 6), XM(nm + 7), XM(nm + 8)), a_nrm.xyz));
  if (dot(n, n) > 0.0) n = normalize(n);
  float P0 = uintBitsToFloat(XR(0x20)), P1 = uintBitsToFloat(XR(0x21)), P2 = uintBitsToFloat(XR(0x22));
  float P3 = uintBitsToFloat(XR(0x23)), P4 = uintBitsToFloat(XR(0x24)), P5 = uintBitsToFloat(XR(0x25));
  vec4 clip = XR(0x26) == 0u ? vec4(P0 * pv.x + P1 * pv.z, P2 * pv.y + P3 * pv.z, P4 * pv.z + P5, -pv.z)
                             : vec4(P0 * pv.x + P1, P2 * pv.y + P3, P4 * pv.z + P5, 1.0);
  /* wide screen modes (gx_render.h GX_XR_WIDE_K): the backend widened the viewport k times; 2D
     (orthographic) draws keep their 4:3 proportions, placed per draw (GX_ANCHOR_* in the normal's w,
     gx_render.c wide_anchor): in the middle, at the left or right edge, or stretched to the width */
  /* field of view setting (gx_render.h GX_XR_FOV): perspective projections see fs times the game's
     vertical angle (taken from the projection), the picture scaled about its centre */
  float fs = uintBitsToFloat(XR(0x7D));
  if (XR(0x26) == 0u && fs > 0.0 && fs != 1.0 && P2 != 0.0) {
    float m = 1.0 / (tan(fs * atan(1.0 / abs(P2))) * abs(P2));
    clip.x = clip.x * m;
    clip.y = clip.y * m;
  }
  if (XR(0x26) != 0u && (XR(0x7F) & 1u) != 0u) {
    float wk = uintBitsToFloat(XR(0x7E));
    float an = a_nrm.w;
    if (wk > 1.0 && an < 2.5) clip.x = clip.x / wk + (an > 1.5 ? 1.0 - 1.0 / wk : an > 0.5 ? 1.0 / wk - 1.0 : 0.0) * clip.w;
  }
  float xs = uintBitsToFloat(XR(0x1A)), ys = uintBitsToFloat(XR(0x1B)), zs = uintBitsToFloat(XR(0x1C));
  float zo = uintBitsToFloat(XR(0x1F));
  gl_Position = vec4(clip.x * sign(xs), clip.y * sign(ys) * CLIP_Y, (2.0 * zo / 16777215.0 - 1.0) * clip.w + 2.0 * zs / 16777215.0 * clip.z, clip.w);
#ifdef DEPTH_ZERO_ONE
  gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);  // Vulkan: clip depth 0..w (GL and deko3d here: -w..w)
#endif
  uint nchan = bits(XR(0x09), 0, 2);
  v_col0 = nchan >= 1u ? channel(0, a_col0, pv, n) : vec4(0.0);
  v_col1 = nchan >= 2u ? channel(1, a_col1, pv, n) : vec4(0.0);
#ifdef PERPIXEL_LIGHT
  v_pv = pv; v_n = n; v_vc0 = a_col0; v_vc1 = a_col1;
#endif
  uint ntex = bits(XR(0x3F), 0, 4);
  v_tex0 = ntex > 0u ? texgen(0, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm0.x) : vec3(0, 0, 1);
  v_tex1 = ntex > 1u ? texgen(1, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm0.y) : vec3(0, 0, 1);
  v_tex2 = ntex > 2u ? texgen(2, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm0.z) : vec3(0, 0, 1);
  v_tex3 = ntex > 3u ? texgen(3, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm0.w) : vec3(0, 0, 1);
  v_tex4 = ntex > 4u ? texgen(4, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm1.x) : vec3(0, 0, 1);
  v_tex5 = ntex > 5u ? texgen(5, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm1.y) : vec3(0, 0, 1);
  v_tex6 = ntex > 6u ? texgen(6, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm1.z) : vec3(0, 0, 1);
  v_tex7 = ntex > 7u ? texgen(7, a_pos.xyz, a_nrm.xyz, v_col0, v_col1, a_tm1.w) : vec3(0, 0, 1);
}
