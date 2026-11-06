# Boost camera effect, NFS-style (plan, 2026-11-06)

User request: while a boost lasts (a single press in Sonic Riders; the duration differs per level), the camera
zooms out and pulls away from the racer, with blur on the sides, so it feels very fast. **Level 1: no change.
Level 2: the effect starts. Level 3: stronger.** Status: **planned, nothing implemented**. Comes after the
ultrawide/FOV work (notes/ultrawide-fov.md), which builds the FOV hook this reuses.

## How racing games sell speed (research, 2026-11-06)

No official EA write-up was found; community and research sources agree on a set of cues:
- **FOV kick / pull-back**: NFS Unbound's nitrous camera pulls back a long way (players joke it "shows the past");
  NFS Carbon mods widen FOV with speed (overtake.gg, nfsmods.xyz "Dynamic Camera FOV").
- **Nitrous-only blur that lingers**: NFS blur on nitrous is tuned separately from normal motion blur and fades
  over a few seconds after the boost (EA forums; NFS Carbon "Motion Blur Control" mod has nitrous values).
- **Radial blur** outward from the centre, strongest at the edges (classic post effect).
- **Camera shake, speed lines/particles that tunnel the view, HUD effects, sound** (Operation Sports on NFS
  Heat/Most Wanted; Shawn Hargreaves on MotoGP: blur tied to camera speed).
- Research caution: strong motion blur can *lower* perceived speed (Holm et al. 2017); higher camera altitude at
  high speed raises it. So: blur only at the edges, keep the centre (the racer) sharp.

## Design

Everything is **render-only** (the game's state is never written), so determinism, replays and online play are
unaffected, and it can be a setting: Boost effect Off / Subtle / Full (default to decide with the user).

Per local player viewport, an intensity `I` (0..1) eases in over ~0.2 s when a boost starts, holds while it lasts,
and eases out over ~0.5 s after (the NFS linger). Target: level 1 = 0, level 2 = 0.6, level 3 = 1.0.

| Cue | How, in the port | At I = 1 (start values, tune by eye) |
|---|---|---|
| FOV widen ("zoom out") | the FOV hook at `fn_800645DC` (r4) from the ultrawide work, scaled by 1 + 0.15 I for that viewport | +15% FOV |
| Pull-back | renderer: for perspective draws of that viewport, an extra view-space translation along the view axis (camera moves back and slightly up) before projection | ~1.5 m back, small rise |
| Edge radial blur | a post pass on the EFB before present: 8-12 taps toward the viewport centre, weight 0 inside an ellipse around the racer, rising to the edges | blur radius ~3% of width at the edges |
| Speed lines | optional overlay: thin streaks at the edges, fading in with I (fits Sonic's style) | sparse, white, 30-50% alpha |
| Edge colour fringe / vignette | in the same post pass: slight chromatic offset and darkening at the edges | subtle |
| Shake | tiny view jitter, a few pixels, low frequency | optional |

## Steps

1. **Find the state** (RAM tools, an input route with boosts at each level, `SR_DETCLOCK` so frames repeat):
   the racer's current level (1-3) and a "boost active" flag or timer in the player record (`0x81A0D740`,
   8 x 0x1080), and which viewport shows which player (split screen). Record in game-addresses.md.
2. **Intensity driver** (host code, read-only reads each frame) + `SR_BOOSTFX` debug variable to force I.
3. **FOV + pull-back** first (cheap, biggest effect); check the HUD (orthographic) is untouched, the sky dome and
   EFB copies (the game's own speed blur, #3; reflections) still line up, and culling at the widened edges.
4. **Edge blur post pass**: GL, Vulkan (Android) and deko3d (Switch), shared GLSL following the uam rules (no
   runtime vector indexing; `per_warp_scratch 0`). Cost check: Switch GPU at 1280x1056 (target < 1 ms per
   frame), Android GPU on the S23 Ultra.
5. **Tune with captures** (`SR_SCREENSHOT` at boost frames; a short side-by-side clip for the user), then the
   setting in the menu (Gameplay or Display tab), split-screen check (one centre per viewport), and the user's
   play test.

## Open questions for the user

- Default on or off? Include speed lines and shake, or only FOV, pull-back and blur?
- Also on the AI racers' boosts? (Proposed: only the local players' own viewports.)
