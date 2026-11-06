# Ultrawide without stretching, and a FOV setting (plan, 2026-11-06)

Requested by the user (3440x1440 monitor, 21.5:9) and in GitHub Discussions #15 ("UltraWidescreen", ultrawide
+ FOV switching). Status: **planned, nothing implemented**. Order: before online play (user, 2026-11-06).

## What exists

- `config/GXEE8P/patches.txt` "Widescreen (any aspect)": multiplies the camera aspect by `g_aspect_scale`
  (target / (4/3)) at `fn_800645DC` entry (f1 = aspect) and after the race camera's aspect `fmadds` (REL
  `81941218`). `settings.c`: `SCREEN_WIDE` sets it to 16:9; `settings_display_aspect()` is 4:3 or 16:9.
- The EFB is 4:3-shaped (`EFB_WIDTH` 640 x 528, times `EFB_SCALE`) and the present pass stretches it to the
  display aspect (`render_gl.c` ~line 327/361; Android `render_vk.c`; Switch deko3d). So 16:9 today = Hor+
  3D squeezed into a 4:3 image, then stretched back: the 3D is right, the 2D layer (HUD, menus) is stretched
  1.33x and horizontal resolution is lower than vertical.
- FOV input: `fn_800645DC(r3 = matrix, r4 = field of view as an integer angle, f1 = aspect, f2/f3 = near/far)`
  converts r4 to float (int->float via the 0x43300000 trick, times a constant) and calls the perspective
  builder [confirmed by reading the generated code]. The race camera computes its aspect itself at
  `81941218`; whether it also goes through `fn_800645DC` for its FOV is **not checked** [uncertain].

## Plan

1. **"Fit window" screen mode** (settings, all platforms): display aspect = the window's (or screen's) aspect,
   clamped to 4:3..32:9; `g_aspect_scale` follows it, also on window resize. Keep the old 3 modes. 16:10 on the
   Steam Deck falls out of this.
2. **Wider EFB instead of stretching** (the "no stretching" part): EFB width = 640 x k x scale with
   k = aspect / (4/3); map GX viewport and scissor X from game units to the wider target (x' = x * k); EFB
   copies (`GXCopyTex` sources: speed blur, screen effects) and peeks scaled the same way; present without
   horizontal stretch. Check: Switch/Android memory (a 2x EFB at 21:9 is ~2400 x 1056), per-backend.
3. **2D layer at its own proportions**: for orthographic projections (GX projection type 1) the renderer
   scales X by 1/k around the centre, so HUD and menus keep 4:3 proportions in the middle. Side areas then
   show what the game drew there (3D in races; clear colour or nothing in 2D-only menus). Option in settings:
   "HUD: stretched / original proportions". Full-screen 2D (fades, menu backgrounds, the speed-blur overlay)
   may need "stretch this quad" detection (a quad covering the whole 640-wide screen gets k instead of 1).
4. **Edge-anchored HUD (later, separate step)**: move left/right HUD groups to the screen edges: needs the
   HUD drawing functions found and a per-element offset. Not part of the first step.
5. **FOV setting**: a patch at `fn_800645DC` entry scaling r4 by `g_fov_scale` (setting: 80%-130%, default
   100%), plus the race camera path if it does not go through that function. Must stay out of online
   sessions or be the same on both sides (determinism: it only changes rendering, but check the game logic does
   not read the projection, e.g. for culling or the lens flare).
6. **Checks**: culling at the widened edges (objects popping in the side areas), lens flare/EFB Z peeks (#7),
   speed blur (#3), split screen 2-4P (each viewport widens), the det route identical (render-only change),
   frames from `SR_SCREENSHOT` at 21:9 and 32:9 (window size via `SR_WINDOW`? add if missing), Switch handheld
   still 16:9, Android phone aspect (~20:9) as a free bonus.
