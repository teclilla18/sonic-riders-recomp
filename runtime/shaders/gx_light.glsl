// GX lighting (XF colour channels): pasted by tools/embed_shaders.py where gx.vert has //@gx_light, and
// into gx.frag for the PC build only (PERPIXEL_LIGHT: per-pixel lighting, render_gl.c)
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
#ifdef GX_TEST_NOLIGHT
  return vec4(rgb, a);  /* GPU cost test (SR_GPU_AB=2): what the lighting costs */
#endif
  if (bits(cc, 1, 1) != 0u) rgb = mat * lights(cc, pos, n, vec4(bits(cc, 6, 1) != 0u ? vcol.rgb : ambr.rgb, 0.0)).rgb;
  if (bits(ac, 1, 1) != 0u) a = mata * lights(ac, pos, n, vec4(0.0, 0.0, 0.0, bits(ac, 6, 1) != 0u ? vcol.a : ambr.a)).a;
  return vec4(rgb, a);
}
