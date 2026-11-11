# Picture options on Linux, Android and Switch (plan, 2026-11-10)

Kind: Draft (design, nothing implemented yet). Goal (user, 2026-11-10): the PC's F1 > Display set (resolution,
anti-aliasing, FSR 1, bloom, colour presets + sliders, character lighting) on every platform as options, same names
and saved keys, light defaults. Replaces the old "PC only" rule. iOS later (Mac session).

## What exists (main at 969864d)

- Settings keys are shared already (`settings.h`): `pc_scale`, `msaa`, `sharpness`, `bloom`, `grade_*`, `char_light`;
  defaults are off / neutral. Switch and Vulkan use `resolution` (RES_1X / 1_5X / 2X, `settings_internal_scale`).
- Linux / Steam Deck: same `render_gl.c`; the menu block in `menu.cpp` `tab_display` is `#elif !defined(SR_PHONE)`, so
  it already shows everything. Only a test is owed.
- Android (and iOS: same `render_vk.c` on MoltenVK): renderer honours 1x / 1.5x / 2x, but the phone menu shows no
  picture options. Switch: only the 1x / 1.5x / 2x radio.
- 3x / 4x stay PC/Linux only: both other backends allocate the EFB at `EFB_SCALE 2`.

## Order agreed with the user

1. Linux check (WSL build + captures). 2. Shared menu block. 3. Android: resolution radio, FSR 1, colour, bloom.
4. Switch: the same four. 5. MSAA on both. 6. Character lighting on both (needs a second vertex shader with the
lighting varyings and lit variants of the specialized fragment shaders in `gen_shaders.py`; riskiest).
A feature shows in a platform's menu only once it works there. Release with what is tested and played.

## Android design (render_vk.c), worked out, not written

Everything rides on the existing `quad()` / `quad_pipe_make` machinery (one sampler, 80-byte push constants: rect, uv,
color, shape, rot). New quad kinds appended after `Q_TOUCH` (old pipeline-key files stay valid), new fragment
shaders listed in `embed_shaders.py` `PROGRAMS_VK` + `configure.py` `shader_src`, modules next to `g_sm_tex`:

- `vk_bright.frag`: bright pass with 16 taps (4x4 grid over the output pixel's footprint; the EFB has no mips, GL
  used a mip level). `color.xy` = tap step in uv, `shape.zw` = uv clamp.
- `vk_blur.frag`: the GL 9-tap Gaussian (5 fetches); `color.xy` = direction, `shape.zw` = uv clamp (targets are
  allocated at the largest size and used top-left, so sampling must clamp to the used part).
- Bloom composite: `vk_tex.frag` with a screen blend (src ONE_MINUS_DST_COLOR, dst ONE, RGB mask), one quad per blur
  level, tint = bloom * 0.72. Slightly different from GL's clamp(b0 + b1) [fine].
- `vk_grade.frag`: `color` = GL's `u_grade`. Needs a copy of the EFB's used part (`vkCmdCopyImage` into a lazily
  made scene image), then a quad back into the EFB with an RGB mask (EFB alpha kept).
- `vk_easu.frag` / `vk_rcas.frag`: ports of `host/fsr1_gl.h`, pixel position from `v_uv * size` instead of
  `gl_FragCoord` (the swapchain may be pre-rotated). EASU reads the EFB directly (`shape.xy` = origin texel,
  `color.xy` = source size) into an intermediate image of the picture's size; RCAS draws that to the screen in
  `present_picture`. EASU only when the picture is larger than the source, else a linear quad (as GL).
- Targets are `TGT_TEX` (RGBA8, no depth), also for the EFB while compositing: `efb_end()`, `to_layout`,
  `target_begin(view, w, h, 0)`. After it, `efb_begin()`'s "fresh" path rebinds buffers and sets.
- Hook: copy `render_gl.c` `execute`'s `post_at` scan (after the last 3D draw, `xfr[0x26] == 0`, before each XFB
  copy); bloom only when `f->in_race`. New images: lazy, own memory, `img_destroy` in `device_teardown`.

## Switch (render_deko.c), to design

Same passes as dksh (`PROGRAMS_DK`); presents with `dkCmdBufBlitImage` today, so FSR needs a shader present path.
Rules: no runtime vector / local-array indexing, every dksh `per_warp_scratch 0`. Test on hardware with mode,
clocks and `[perf]` per feature on and off.
