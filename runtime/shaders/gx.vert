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

#ifdef XF_UBO
vec4 row(int a) { return uintBitsToFloat(xf[xfi(a) >> 2]); }  /* a is a multiple of 4: one vector */
#else
vec4 row(int a) { return vec4(XM(a), XM(a + 1), XM(a + 2), XM(a + 3)); }
#endif
/* the lit sum (ambient + lights), clamped; colour channels use .rgb, alpha channels .a. Returning a
   vec4 for both: uam (deko3d) mixed up components of the old `alpha ? vec3(sum.a) : sum.rgb` once
   both calls were inlined, and characters came out teal on the Switch (red lost; #11); the loop also
   keeps no vec4 running sum seeded with the ambient and has no `continue` */
vec4 lights(uint ctrl, vec3 pos, vec3 n, vec4 amb) {
  vec3 acc = vec3(0.0); float acca = 0.0;  /* the lights' part; the ambient is added after the loop */
  uint mask = bits(ctrl, 2, 4) | (bits(ctrl, 11, 4) << 4);
  uint diff_fn = bits(ctrl, 7, 2); bool atten = bits(ctrl, 9, 1) != 0u; bool spot = bits(ctrl, 10, 1) != 0u;
  for (int i = 0; i < 8; i++) {
    if ((mask & (1u << uint(i))) != 0u) {
    int b = 0x600 + i * 16;
    vec4 lc = rgba8(XMU(b + 3));
    vec3 ca = vec3(XM(b + 4), XM(b + 5), XM(b + 6)), da = vec3(XM(b + 7), XM(b + 8), XM(b + 9));
    vec3 lp = vec3(XM(b + 10), XM(b + 11), XM(b + 12)), ld = vec3(XM(b + 13), XM(b + 14), XM(b + 15));
    vec3 l; float att = 1.0;
    if (atten && spot) {
      l = lp - pos; float d2 = dot(l, l); float d = sqrt(d2); l = l / max(d, 1e-6);
      float c = max(0.0, dot(l, ld));
      att = max(0.0, ca.x + ca.y * c + ca.z * c * c) / max(1e-6, da.x + da.y * d + da.z * d2);
    } else if (atten) {
      /* specular: the light's direction from the vertex, as Dolphin. normalize(lp) alone was NaN for the
         usual specular light at (0, 0, 0) and the term vanished: characters had no highlights (#11) */
      l = DEBUG_TEV == 7 ? normalize(lp) : normalize(lp - pos);  /* 7: the old way (debugging) */
      float c = dot(n, l) >= 0.0 ? max(0.0, dot(n, ld)) : 0.0;
      att = max(0.0, ca.x + ca.y * c + ca.z * c * c) / max(1e-6, da.x + da.y * c + da.z * c * c);
    } else {
      l = normalize(lp - pos);
    }
    float df = dot(l, n);
    df = diff_fn == 0u ? 1.0 : (diff_fn == 1u ? df : max(0.0, df));
    acc += (att * df) * lc.rgb; acca += (att * df) * lc.a;
    }
  }
  return clamp(amb + vec4(acc, acca), 0.0, 1.0);
}
vec4 channel(int c, vec4 vcol, vec3 pos, vec3 n) {
  uint cc = XR(0x0E + c), ac = XR(0x10 + c);
  vec4 matr = rgba8(XR(0x0C + c)), ambr = rgba8(XR(0x0A + c));
  vec3 mat = bits(cc, 0, 1) != 0u ? vcol.rgb : matr.rgb;
  float mata = bits(ac, 0, 1) != 0u ? vcol.a : matr.a;
  vec3 rgb = mat; float a = mata;
  if (bits(cc, 1, 1) != 0u) rgb = mat * lights(cc, pos, n, vec4(bits(cc, 6, 1) != 0u ? vcol.rgb : ambr.rgb, 0.0)).rgb;
  if (bits(ac, 1, 1) != 0u) a = mata * lights(ac, pos, n, vec4(0.0, 0.0, 0.0, bits(ac, 6, 1) != 0u ? vcol.a : ambr.a)).a;
  return vec4(rgb, a);
}
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
  float xs = uintBitsToFloat(XR(0x1A)), ys = uintBitsToFloat(XR(0x1B)), zs = uintBitsToFloat(XR(0x1C));
  float zo = uintBitsToFloat(XR(0x1F));
  gl_Position = vec4(clip.x * sign(xs), clip.y * sign(ys) * CLIP_Y, (2.0 * zo / 16777215.0 - 1.0) * clip.w + 2.0 * zs / 16777215.0 * clip.z, clip.w);
  uint nchan = bits(XR(0x09), 0, 2);
  v_col0 = nchan >= 1u ? channel(0, a_col0, pv, n) : vec4(0.0);
  v_col1 = nchan >= 2u ? channel(1, a_col1, pv, n) : vec4(0.0);
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
