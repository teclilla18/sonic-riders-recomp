// GX ubershader (part of the shared GLSL; see runtime/shaders/README.md for the prelude macros)
VARY(0) in vec4 v_col0;
VARY(1) in vec4 v_col1;
VARY(2) in vec3 v_tex0;
 VARY(3) in vec3 v_tex1;
 VARY(4) in vec3 v_tex2;
 VARY(5) in vec3 v_tex3;

VARY(6) in vec3 v_tex4;
 VARY(7) in vec3 v_tex5;
 VARY(8) in vec3 v_tex6;
 VARY(9) in vec3 v_tex7;

SAMPLER(0) uniform sampler2D u_tex0;
SAMPLER(1) uniform sampler2D u_tex1;
SAMPLER(2) uniform sampler2D u_tex2;
SAMPLER(3) uniform sampler2D u_tex3;
SAMPLER(4) uniform sampler2D u_tex4;
SAMPLER(5) uniform sampler2D u_tex5;
SAMPLER(6) uniform sampler2D u_tex6;
SAMPLER(7) uniform sampler2D u_tex7;
layout(location = 0) out vec4 o_color;
vec3 coord(uint i) {
  return i == 0u ? v_tex0 : i == 1u ? v_tex1 : i == 2u ? v_tex2 : i == 3u ? v_tex3 :
         i == 4u ? v_tex4 : i == 5u ? v_tex5 : i == 6u ? v_tex6 : v_tex7;
}
vec4 sample_map(uint m, vec2 uv) {
  if (m == 0u) return texture(u_tex0, uv); if (m == 1u) return texture(u_tex1, uv);
  if (m == 2u) return texture(u_tex2, uv); if (m == 3u) return texture(u_tex3, uv);
  if (m == 4u) return texture(u_tex4, uv); if (m == 5u) return texture(u_tex5, uv);
  if (m == 6u) return texture(u_tex6, uv); return texture(u_tex7, uv);
}
// No runtime indexing of vectors or local arrays below: uam (deko3d) miscompiles it or spills it
// to scratch memory (see gx_common.glsl), so components and TEV inputs are picked with selects.
float comp(vec4 v, uint c) { return c == 0u ? v.x : c == 1u ? v.y : c == 2u ? v.z : v.w; }
vec4 swizzle(vec4 c, uint table) {
  uint lo = BP(0xF6 + int(table) * 2), hi = BP(0xF7 + int(table) * 2);
  return vec4(comp(c, bits(lo, 0, 2)), comp(c, bits(lo, 2, 2)), comp(c, bits(hi, 0, 2)), comp(c, bits(hi, 2, 2)));
}
float s11(uint v) { return float(int(v << 21u) >> 21) / 255.0; }
vec4 tev_reg(int r) {  /* r: 0 PREV, 1-3 C0-C2 (initial values) */
  uint ra = BP(0xE0 + 2 * r), bg = BP(0xE1 + 2 * r);
  return vec4(s11(bits(ra, 0, 11)), s11(bits(bg, 12, 11)), s11(bits(bg, 0, 11)), s11(bits(ra, 12, 11)));
}
vec4 konst_reg(int k) {
  uint ra = KR(2 * k), bg = KR(2 * k + 1);
  return vec4(float(bits(ra, 0, 8)), float(bits(bg, 12, 8)), float(bits(bg, 0, 8)), float(bits(ra, 12, 8))) / 255.0;
}
vec3 kcolor(uint sel) {
  if (sel < 8u) return vec3(1.0 - float(sel) / 8.0);
  if (sel >= 12u && sel < 16u) return konst_reg(int(sel) - 12).rgb;
  if (sel >= 16u) { vec4 k = konst_reg(int(sel & 3u)); return vec3(comp(k, (sel - 16u) >> 2)); }
  return vec3(0.0);
}
float kalpha(uint sel) {
  if (sel < 8u) return 1.0 - float(sel) / 8.0;
  if (sel >= 16u) { vec4 k = konst_reg(int(sel & 3u)); return comp(k, (sel - 16u) >> 2); }
  return 0.0;
}
bool acmp(float a, uint f, float ref) {
  if (f == 0u) return false; if (f == 1u) return a < ref; if (f == 2u) return a == ref; if (f == 3u) return a <= ref;
  if (f == 4u) return a > ref; if (f == 5u) return a != ref; if (f == 6u) return a >= ref; return true;
}
float q8(float v) { return floor(clamp(v, 0.0, 1.0) * 255.0 + 0.5); }
vec4 reg(vec4 r0, vec4 r1, vec4 r2, vec4 r3, uint i) { return i == 0u ? r0 : i == 1u ? r1 : i == 2u ? r2 : r3; }
/* TEV color input (0-15): PREV, APREV, C0, A0, C1, A1, C2, A2, TEXC, TEXA, RASC, RASA, ONE, HALF, KONST, ZERO */
vec3 cinput(uint i, vec4 r0, vec4 r1, vec4 r2, vec4 r3, vec4 tex, vec4 ras, vec3 kc) {
  if (i < 8u) { vec4 r = reg(r0, r1, r2, r3, i >> 1); return (i & 1u) != 0u ? vec3(r.a) : r.rgb; }
  if (i == 8u) return tex.rgb; if (i == 9u) return vec3(tex.a);
  if (i == 10u) return ras.rgb; if (i == 11u) return vec3(ras.a);
  if (i == 12u) return vec3(1.0); if (i == 13u) return vec3(0.5);
  if (i == 14u) return kc; return vec3(0.0);
}
/* TEV alpha input (0-7): APREV, A0, A1, A2, TEXA, RASA, KONST, ZERO */
float ainput(uint i, vec4 r0, vec4 r1, vec4 r2, vec4 r3, vec4 tex, vec4 ras, float ka) {
  if (i < 4u) return reg(r0, r1, r2, r3, i).a;
  if (i == 4u) return tex.a; if (i == 5u) return ras.a; if (i == 6u) return ka; return 0.0;
}
void main() {
  vec4 r0 = tev_reg(0), r1 = tev_reg(1), r2 = tev_reg(2), r3 = tev_reg(3);
  int stages = int(bits(BP(0x00), 10, 4)) + 1;
  for (int s = 0; s < 16; s++) {
    if (s >= stages) break;
    uint order = BP(0x28 + s / 2) >> uint((s & 1) * 12);
    uint cenv = BP(0xC0 + 2 * s), aenv = BP(0xC1 + 2 * s);
    vec4 tex = vec4(0.0);
    if (bits(order, 6, 1) != 0u) {
      vec3 tc = coord(bits(order, 3, 3));
      tex = swizzle(sample_map(bits(order, 0, 3), tc.xy / (tc.z == 0.0 ? 1.0 : tc.z)), bits(aenv, 2, 2));
    }
    uint ch = bits(order, 7, 3);
    vec4 ras = ch == 0u ? v_col0 : ch == 1u ? v_col1 : vec4(0.0);
    ras = swizzle(clamp(ras, 0.0, 1.0), bits(aenv, 0, 2));
    uint ksel = BP(0xF6 + s / 2);
    vec3 kc = kcolor(bits(ksel, (s & 1) != 0 ? 14 : 4, 5));
    float ka = kalpha(bits(ksel, (s & 1) != 0 ? 19 : 9, 5));
    /* color */
    vec3 ca = clamp(cinput(bits(cenv, 12, 4), r0, r1, r2, r3, tex, ras, kc), 0.0, 1.0);
    vec3 cb = clamp(cinput(bits(cenv, 8, 4), r0, r1, r2, r3, tex, ras, kc), 0.0, 1.0);
    vec3 cc = clamp(cinput(bits(cenv, 4, 4), r0, r1, r2, r3, tex, ras, kc), 0.0, 1.0);
    vec3 cd = cinput(bits(cenv, 0, 4), r0, r1, r2, r3, tex, ras, kc);
    uint cbias = bits(cenv, 16, 2), cop = bits(cenv, 18, 1), cscale = bits(cenv, 20, 2);
    vec3 cres;
    if (cbias != 3u) {
      vec3 l = mix(ca, cb, cc);
      cres = cd + (cop != 0u ? -l : l) + (cbias == 1u ? 0.5 : cbias == 2u ? -0.5 : 0.0);
      cres *= cscale == 0u ? 1.0 : cscale == 1u ? 2.0 : cscale == 2u ? 4.0 : 0.5;
    } else {
      uint mode = (cscale << 1) | cop; bool t;
      vec3 qa = vec3(q8(ca.r), q8(ca.g), q8(ca.b)), qb = vec3(q8(cb.r), q8(cb.g), q8(cb.b));
      if (mode < 6u) {
        float va = mode < 2u ? qa.r : mode < 4u ? qa.r + qa.g * 256.0 : qa.r + qa.g * 256.0 + qa.b * 65536.0;
        float vb = mode < 2u ? qb.r : mode < 4u ? qb.r + qb.g * 256.0 : qb.r + qb.g * 256.0 + qb.b * 65536.0;
        t = (mode & 1u) != 0u ? va == vb : va > vb;
        cres = cd + (t ? cc : vec3(0.0));
      } else {
        bvec3 bt = (mode & 1u) != 0u ? equal(qa, qb) : greaterThan(qa, qb);
        cres = cd + vec3(bt.x ? cc.r : 0.0, bt.y ? cc.g : 0.0, bt.z ? cc.b : 0.0);
      }
    }
    cres = bits(cenv, 19, 1) != 0u ? clamp(cres, 0.0, 1.0) : clamp(cres, -1024.0 / 255.0, 1023.0 / 255.0);
    /* alpha */
    float aa = clamp(ainput(bits(aenv, 13, 3), r0, r1, r2, r3, tex, ras, ka), 0.0, 1.0);
    float ab = clamp(ainput(bits(aenv, 10, 3), r0, r1, r2, r3, tex, ras, ka), 0.0, 1.0);
    float ac = clamp(ainput(bits(aenv, 7, 3), r0, r1, r2, r3, tex, ras, ka), 0.0, 1.0);
    float ad = ainput(bits(aenv, 4, 3), r0, r1, r2, r3, tex, ras, ka);
    uint abias = bits(aenv, 16, 2), aop = bits(aenv, 18, 1), ascale = bits(aenv, 20, 2);
    float ares;
    if (abias != 3u) {
      float l = mix(aa, ab, ac);
      ares = ad + (aop != 0u ? -l : l) + (abias == 1u ? 0.5 : abias == 2u ? -0.5 : 0.0);
      ares *= ascale == 0u ? 1.0 : ascale == 1u ? 2.0 : ascale == 2u ? 4.0 : 0.5;
    } else {
      uint mode = (ascale << 1) | aop; bool t;
      if (mode >= 6u) t = (mode & 1u) != 0u ? q8(aa) == q8(ab) : q8(aa) > q8(ab);
      else t = (mode & 1u) != 0u ? q8(ca.r) == q8(cb.r) : q8(ca.r) > q8(cb.r);
      ares = ad + (t ? ac : 0.0);
    }
    ares = bits(aenv, 19, 1) != 0u ? clamp(ares, 0.0, 1.0) : clamp(ares, -1024.0 / 255.0, 1023.0 / 255.0);
    uint cdst = bits(cenv, 22, 2), adst = bits(aenv, 22, 2);
    if (cdst == 0u) r0.rgb = cres; else if (cdst == 1u) r1.rgb = cres; else if (cdst == 2u) r2.rgb = cres; else r3.rgb = cres;
    if (adst == 0u) r0.a = ares; else if (adst == 1u) r1.a = ares; else if (adst == 2u) r2.a = ares; else r3.a = ares;
  }
  vec4 o = clamp(r0, 0.0, 1.0);
  /* alpha test */
  uint ac = BP(0xF3);
  float a8 = q8(o.a);
  bool t0 = acmp(a8, bits(ac, 16, 3), float(bits(ac, 0, 8))), t1 = acmp(a8, bits(ac, 19, 3), float(bits(ac, 8, 8)));
  uint lg = bits(ac, 22, 2);
  bool pass = lg == 0u ? (t0 && t1) : lg == 1u ? (t0 || t1) : lg == 2u ? (t0 != t1) : (t0 == t1);
  if (!pass) discard;
  uint cm1 = BP(0x42);
  if (bits(cm1, 8, 1) != 0u) o.a = float(bits(cm1, 0, 8)) / 255.0;
  else if (bits(BP(0x43), 0, 3) != 1u) o.a = 1.0;  /* EFB formats without alpha */
  o_color = o;
}
