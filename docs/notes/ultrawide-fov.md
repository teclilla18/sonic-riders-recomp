# Ultrawide without stretching, edge HUD, and a FOV setting (2026-11-06)

Requested by the user (3440x1440 monitor, 21.5:9) and in GitHub Discussions #15. Released in v0.5.0.
Status: tested on PC (det route 16:9 / 21:9 / 32:9 + 2P split, menu route and race pause at 21:9), Switch in
Eden (Fit = 16:9), Android emulator (20:9); **not yet seen by the user, not on Switch hardware or a phone**.

## Settings (settings.c, menu.cpp Display tab)

- Screen modes: 4:3, 16:9 widescreen, **Fit window** (`SCREEN_FIT` = 3, the window's or screen's aspect, clamped
  4:3..32:9; Switch = 16:9, Steam Deck 16:10, phones ~20:9). Value 2 was "16:9 stretched" (removed 2026-11-06,
  user's decision): a saved 2 or `SR_SCREEN=2` reads as 16:9 (`SCREEN_REMOVED_2`). Aspects are rounded so the EFB
  is an even number of pixels wide (`round_aspect`); 16:9 and Fit in a 16:9 window give the same camera aspect.
- `hud_stretch` (16:9 only, default off): 2D at 4:3 in the middle, or stretched to the width. Fit window always
  puts the race HUD at the edges (no option, user's decision).
- `fov` 80..130 % (default 100).
- Test overrides (not saved): `SR_SCREEN=0..3`, `SR_FOV=80..130`, `SR_HUDSTRETCH=0/1`; PC `SR_WINDOW=WxH`.
- Online sessions / recordings / replays fix the camera aspect for the run (`settings_fix_wide_k`, below).

## How it works

- Camera: the aspect patches (`config/GXEE8P/patches.txt`, `g_aspect_scale` = k) make the 3D Hor+.
- Wide EFB: k = display aspect / (4/3). `gx_render.c` frame start stamps k, a flags word and the FOV scale into
  unused XF registers 0x7E / 0x7F / 0x7D of every state (`GX_XR_*` in gx_render.h) and `GxFrame.wide_k`.
  Backends scale viewport, scissor, EFB-copy rects, copy clears and the XFB present by k in X.
- **2D placement, per draw** (gx_render.c `wide_anchor`, 2026-11-06): for each orthographic draw (decoded on the
  GP thread, not the helper), its clip-space X/Y extent is computed on the CPU from the vertices, the position
  matrix and the projection. The anchor goes into every vertex's `nrm[3]` (unused; zeroed by the decoder) and
  gx.vert applies it: `GX_ANCHOR_MIDDLE` x/k, `LEFT` x/k + (1/k - 1), `RIGHT` x/k + (1 - 1/k), `STRETCH` x.
  - A draw spanning the width (x <= -0.99 and >= 0.99): stretched (fades, backgrounds, the speed-blur copy).
  - **Movies** (2026-11-07, `is_movie_draw`): the movie player draws each frame as one screen-sized quad, one
    TEV stage, map 0 = a 640x480 RGBA8 texture in RAM (CPU-decoded) that is not an EFB copy target
    (`gx_texture_is_copy`). That draw gets MIDDLE (4:3, black bars) unless HUD_43 is off (stretch HUD).
    Subtitles stay as they were (band stretched, letters middle). Seen: story intro movie.
  - **Bands**: draws side by side (X ranges meet / overlap at most half the narrower one, Y ranges overlap at least
    half the taller one, each at least 0.1 clip units wide) that together cover the width are all stretched
    (the title's top/bottom bands, menu bars). The last 64 2D draws of a frame are remembered; earlier draws are
    re-marked (the frame is drawn only once complete). Narrow draws (letters of a text line) never join.
    Pieces 0.15+ high must also be within 10% of the same height (2026-11-07, v0.5.2+): the Track Select row
    (striped panel 0.45, arrow panels 0.39 to x +-1.17, course pictures 0.35) joined into a band and the outer
    pictures were stretched out of their frame. `SR_ANCHORLOG` also prints "draw j touches draw m".
  - Edges (`GX_WIDE_HUD_EDGES`): only in Fit window **and** a race: `GAME_TOP_MODE` == 8 and `GAME_MENU_FLAG`
    (0x8021C1AC) 0, with 8 menu frames in a row needed to leave (the flag blips for single frames in races).
    Mode 8 alone stayed on in menus after story mode and tore the Extras, Theater subtitles and help lines
    apart (user's v0.5.0 screenshots, 2026-11-07). A draw whose middle is left of -0.3 goes left, right of
    +0.3 goes right, else middle. Menus stay 4:3 in the middle: edge anchoring splits icon rows.
  - **Text lines** (2026-11-07): the game draws each letter as its own draw. Consecutive narrow draws
    (< 0.15 wide) on the same row (Y within a quarter of the height, gaps under 0.15) are one line and get the
    anchor of the line's middle (the race's quit dialog was torn apart before).
  - Bands of thin pieces (< 0.15 high) may share only 35 % of their height (the menus' dark bars are offset
    pieces, 43 % shared); bigger pieces still need half (the round menu backgrounds must not join).
  - Flags: Fit = HUD_43 | EDGES (in races); 16:9 = HUD_43, or 0 with hud_stretch (everything stretched).
  - `[wide] frame N: game mode M, HUD flags F` logs each change; `SR_ANCHORLOG=<frame>` logs that frame's 2D draws
    with extents and anchors.
- Why per draw: the old per-vertex rule (vertices at |x| >= 0.998 kept, others /k) squeezed the inner vertices of
  the 18-vertex speed-blur copy quad against its texture, drawing last frame's character beside itself (the user's
  "mirage" in every non-4:3 mode), and stretched edge pieces of menu panels into gradients.
- FOV (gx.vert): perspective draws scale clip x/y by 1 / (tan(fs * atan(1/P2)) * P2). Render-only.
- `fn_800645DC` is not a projection setup (see git history of this note for details).

## Determinism (online play)

- FOV and HUD placement do not change game memory [confirmed, RAM hashes over 6000 det-route steps].
- The camera aspect **does** (even 1.3333 vs 1.3344). Since 2026-11-06 (netplay.c, protocol 3): the host sends its
  k in the welcome message and the joiner uses it (`settings_fix_wide_k`); recordings store k in header bytes
  20-23 and replays use it. Window resizes and menu changes then only change how the picture is shown (bars).
  Not yet tested in a two-machine session.

## Checked / known

- PC 21:9: race HUD at the edges, 2P split HUD at the edges, title bands whole, menus (save select, Player Entry,
  "Are you ready") right, race pause menu right. 32:9 and 16:9 right. Eden and Android emulator same as PC.
- Phones: in Fit window the edge HUD now sits under the touch buttons (menu button over the ring counter, START
  over the map) [seen in the emulator]; may need a touch-layout or default change.
- 2026-11-07 (PC 21:9, det routes): race HUD at the edges, pause menu and quit dialog whole, Normal Race / Mode
  Select / Extras / Gear Gallery menus 4:3 with their bars reaching the edges. v0.5.0 was pulled by the user
  for the bugs above.
- 2026-11-07 (PC 21:9 Fit window, routes in build/uw3/): Options (+ Audio and Language pages), Extra, Audio
  Room, Records, Theater menu, story intro movie with subtitles, a whole 3-lap Free Race at Metal City and
  its results screen; det route at 21:9, 32:9, 16:9 widescreen, 2P split [confirmed by frames]. Fixed:
  movies were stretched (now 4:3, `is_movie_draw`); the results screen was torn like the user's v0.5.0 shot
  (edges now also end at the finish line: `GAME_RACE_PHASE` 0x801DFC80 == 0xA, same 8-frame debounce).
- Not checked: in-engine story cutscenes later in the story (the user's "That's really something" subtitle;
  needs a story save), 3-4P, Tag/Survival, story races, 2P with one player finished (the phase word is global:
  the other player's HUD may leave the edges early). 1-pixel seams in menu bars: not seen at 1680x720.
- Known: big race texts made of wide letters (e.g. "5th STORM" trick ratings, "melon") can still split at the
  edges (letters wider than 0.15 do not join a text line).
- Known: at 32:9 and at FOV 130 % the speed-blur geometry's outline shows at the right edge.
