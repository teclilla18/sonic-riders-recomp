# Ultrawide without stretching, and a FOV setting (2026-11-06)

Requested by the user (3440x1440 monitor, 21.5:9) and in GitHub Discussions #15. Status: **implemented on PC,
Switch and Android; tested on PC (det-route captures), Switch in Eden and the Android emulator; not yet seen by
the user; menu settings not final** (the user asked to see captures first).

## Settings (settings.c, menu.cpp Display tab)

- Screen modes: 4:3, 16:9 widescreen, 16:9 stretched, **Fit window** (`SCREEN_FIT` = 3, the window's or
  screen's aspect, clamped 4:3..32:9; Switch = 16:9, Steam Deck 16:10, phones ~20:9). Aspects are rounded so the
  EFB is an even number of pixels wide (`round_aspect`); 16:9 is rounded the same way (k = 854/640), so 16:9 and
  Fit in a 16:9 window give the game the same camera aspect.
- `hud_stretch` (default off): 2D at 4:3 proportions in the middle, or stretched to the width.
- `fov` 80..130 % (default 100).
- Test overrides (not saved): `SR_SCREEN=0..3`, `SR_FOV=80..130`, `SR_HUDSTRETCH=0/1`; PC `SR_WINDOW=WxH`
  (first window size, for wide captures).

## How it works

- Camera: the existing aspect patches (`config/GXEE8P/patches.txt`, `g_aspect_scale` = k) make the 3D Hor+
  (race frame at 16:9: projection P2 unchanged, P0 / 1.333 [confirmed]).
- Wide EFB instead of stretching: k = display aspect / (4/3). `gx_render.c` frame start stamps k, a flags word
  and the FOV scale into unused XF registers 0x7E / 0x7F / 0x7D of every state (`GX_XR_*` in gx_render.h; game
  writes to them are ignored in gx_fifo.c) and into `GxFrame.wide_k`. Backends scale viewport, scissor, EFB-copy
  source rects, copy clears and the XFB present by k in X (`wx()` in render_gl.c, `XSC()` in render_deko.c and
  render_vk.c). EFB width: PC reallocates on change (`efb_resize`); Switch allocated for 16:9 (854 x 2 columns);
  Android for up to 32:9.
- 2D (gx.vert): orthographic draws get clip x / k (4:3 in the middle) unless a vertex is on or beyond a screen
  edge (|x| >= 0.998): full-width quads (fades, backgrounds, the speed-blur overlay) still fill the screen.
- FOV (gx.vert): perspective draws scale clip x/y by 1 / (tan(fs * atan(1/P2)) * P2): the game's vertical angle
  times fs, about the projection centre. Render-only.
- `fn_800645DC` is **not** a projection setup: it tail-calls `fn_8006462C` -> `fn_8004C9E0`, which builds a
  translation matrix (identity + f1/f2/f3), so its r4 is a translation, not a FOV [confirmed by reading the
  generated code]. The race camera calls `fn_8006462C` too; its patched f1 is used by a later projection call.
  What the `800645DC` entry patch (from the Gecko code) actually scales is [uncertain].

## Determinism (online play)

- FOV and HUD stretch do not change game memory: RAM hashes identical over 6000 det-route steps [confirmed].
- The camera aspect **does**: 4:3 vs 16:9 differ in many RAM blocks from the race start on, and even 1.3333 vs
  1.3344 differ in blocks 1, 2, 3, 17, 26 (the game uses the aspect beyond drawing, probably culling). So every
  machine in an online session must use the same camera aspect (open item for online play; the desync check
  would report it). Note: this was already true for 4:3 vs 16:9 before.

## Checked (PC det route: title 2900, 1P race 5300, 2P race 10500)

- 16:9, 21:9 (3440x1440), 32:9 (3840x1080): 3D wider, HUD 4:3 in the middle, 2P split screen right, perf test
  59.8+ fps both demos [confirmed on PC]. Switch in Eden (16:9): same frame as PC. Android emulator (2400x1080):
  fills the screen; touch buttons now sit over the picture instead of on black bars.
- Known: title screen top/bottom bands show seams near the corners in wide modes (the bands are pieces; those
  touching an edge get stretched); at 32:9 and at FOV 130 % the speed-blur geometry's outline shows at the
  right edge; culling at the widened edges not seen popping in the captures (not proven).
- Not checked: menus beyond the title, story cutscenes, 3-4P, lens flare (#7 is missing anyway).

## Later

- Edge-anchored HUD (left/right groups at the screen edges): needs the HUD drawing functions.

## User review (2026-11-06, PC 3440x1440) and the plan

See STATE.md "Ultrawide + FOV" for the full report and plan: fit window needs an edge-anchored HUD (the only
option there), 16:9 keeps 4:3-middle (default) or stretched HUD, the "16:9 stretched" screen mode goes; bugs: a
colour gradient bottom left in fit mode, and a faint copy ("mirage") of the player's character to its right in
every non-4:3 mode.
