"""Redraw the supplied background picture at K times its size from the measurements in elements.json.

    py -3 tools/bg/render.py <original.jpg> <K> <out.png>

Every element sits where analyse.py found it, with its measured brightness: the smooth base glow (an
edge-preserving copy of the original), the shaded squares, the dotted grid lines, the dots / big squares /
sparkles on the crossings and the light streaks. Only the drawing is new, so it stays sharp at any size."""
import json
import sys

import numpy as np
from PIL import Image
from scipy import ndimage as nd

src, K, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
# optional 4th argument "x0,y0,x1,y1": a repeating-tile box; streaks that cross its edges are left out
TILE = [float(v) for v in sys.argv[4].split(',')] if len(sys.argv) > 4 else None
d = json.load(open('tools/bg/elements.json'))
orig = np.asarray(Image.open(src).convert('RGB')).astype(np.float32)
H, W = d['H'], d['W']
HK, WK = H * K, W * K
rng = np.random.default_rng(7)

# ---- 1. base: the original with every thin element removed (median keeps the square edges), then smoothed
base = np.stack([nd.median_filter(orig[..., c], size=11) for c in range(3)], -1)
base = np.stack([nd.gaussian_filter(base[..., c], 1.2) for c in range(3)], -1)
img = np.asarray(Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).resize((WK, HK), Image.BICUBIC)).astype(np.float32)

cols, rows = d['cols'], d['rows']
px, py = d['px'], d['py']

# ---- 2. shaded squares: crisp edges, measured colour
for s in d['squares']:
    x0, x1 = cols[s['i']], cols[s['i'] + 1]
    y0, y1 = rows[s['j']], rows[s['j'] + 1]
    xa, xb = int(round((x0 + 1.2) * K)), int(round((x1 - 0.6) * K))
    ya, yb = int(round((y0 + 1.2) * K)), int(round((y1 - 0.6) * K))
    xa, ya = max(xa, 0), max(ya, 0)
    xb, yb = min(xb, WK), min(yb, HK)
    if xb <= xa or yb <= ya:
        continue
    # keep the base's gentle gradient inside the square, lifted to the measured mean
    region = img[ya:yb, xa:xb]
    lift = np.array(s['rgb']) - region.reshape(-1, 3).mean(0)
    img[ya:yb, xa:xb] = region + lift

# light layers, added on top (screen-like): one for sharp cores, one for glows
core = np.zeros((HK, WK), np.float32)
glow = np.zeros((HK, WK), np.float32)

def stamp(layer, x, y, w, h, v):
    """Anti-aliased rectangle centred at original-pixel coords (x, y), size w x h original pixels."""
    xa, xb = (x - w / 2) * K, (x + w / 2) * K
    ya, yb = (y - h / 2) * K, (y + h / 2) * K
    ia, ib = int(np.floor(xa)), int(np.ceil(xb))
    ja, jb = int(np.floor(ya)), int(np.ceil(yb))
    if ib <= 0 or jb <= 0 or ia >= WK or ja >= HK:
        return
    xs = np.arange(ia, ib); ys = np.arange(ja, jb)
    cx = np.clip(np.minimum(xs + 1, xb) - np.maximum(xs, xa), 0, 1)
    cy = np.clip(np.minimum(ys + 1, yb) - np.maximum(ys, ya), 0, 1)
    m = np.outer(cy, cx) * v
    ia2, ja2 = max(ia, 0), max(ja, 0)
    ib2, jb2 = min(ib, WK), min(jb, HK)
    sub = m[ja2 - ja:jb2 - ja, ia2 - ia:ib2 - ia]
    np.maximum(layer[ja2:jb2, ia2:ib2], sub, out=layer[ja2:jb2, ia2:ib2])

# ---- 3. dotted grid lines: 12 dots per cell, phase locked to the crossings
dots = 12
step_x, step_y = px / dots, py / dots
dot_v = 118.0
for y in rows:
    for k in range(int(-px), int(W / step_x) + 13):
        x = cols[0] + k * step_x
        if -2 < x < W + 2 and k % dots:
            stamp(core, x, y, 1.15, 1.15, dot_v)
for x in cols:
    for k in range(int(-py), int(H / step_y) + 13):
        y = rows[0] + k * step_y
        if -2 < y < H + 2 and k % dots:
            stamp(core, x, y, 1.15, 1.15, dot_v)

# ---- 4. crossings
quiet = float(np.median([c['c'] for c in d['cross']]))
arm_base = float(np.median([min(c['ah'], c['av']) for c in d['cross']]))
for c in d['cross']:
    x, y = c['x'], c['y']
    bright = min(255.0, c['c'])
    if c['blob'] >= 22 and c['d2'] > 125:     # the bigger pale squares (filled corners, unlike a sparkle)
        side = min(7.5, max(5.0, c['blob'] ** 0.5 + 0.5))
        stamp(core, x, y, side, side, bright * 0.86)
        stamp(glow, x, y, side + 2, side + 2, bright * 0.22)
    else:                                     # the small dots
        side = 2.4 if bright > quiet else 2.0
        stamp(core, x, y, side, side, bright * 0.62)
        stamp(glow, x, y, 4, 4, bright * 0.12)
    eh, ev = c['ah'] - arm_base, c['av'] - arm_base
    if min(eh, ev) > 38 and c['diag'] < 45:  # four-point sparkle
        length = 12.5
        v = min(255.0, 90 + 1.1 * min(eh, ev))
        for t in np.linspace(0, 1, 18):       # arms taper towards their tips
            off = t * length
            vv = v * (1 - t) ** 1.4
            for sx, sy in ((off, 0), (-off, 0), (0, off), (0, -off)):
                stamp(core, x + sx, y + sy, 1.0, 1.0, vv)
        stamp(glow, x, y, 6, 6, v * 0.5)

# ---- 5. streaks: the measured brightness profile along the line, as a thin core plus a soft glow
lvl = d['line_level']
for s in d['streaks']:
    if TILE:
        lo, hi = (TILE[0], TILE[2]) if s['axis'] == 'h' else (TILE[1], TILE[3])
        pmin, pmax = (TILE[1], TILE[3]) if s['axis'] == 'h' else (TILE[0], TILE[2])
        if pmin - 3 < s['pos'] < pmax + 3 and (s['start'] - 3 < lo < s['end'] + 3 or s['start'] - 3 < hi < s['end'] + 3):
            continue
    prof = np.array(s['profile'], np.float32)
    n = len(prof)
    ts = np.linspace(s['start'], s['end'] - 1, (n - 1) * K + 1)
    vals = np.interp(ts, np.arange(s['start'], s['end']), prof)
    vals = np.clip((vals - lvl) * 1.45, 0, 255)
    for t, v in zip(ts, vals):
        if v < 4:
            continue
        if s['axis'] == 'h':
            stamp(core, t, s['pos'], 1.0 / K + 0.001, 1.5, min(255, v + 40))
            stamp(glow, t, s['pos'], 1.0 / K + 0.001, 4.0, v * 0.75)
        else:
            stamp(core, s['pos'], t, 1.5, 1.0 / K + 0.001, min(255, v + 40))
            stamp(glow, s['pos'], t, 4.0, 1.0 / K + 0.001, v * 0.75)

core = nd.gaussian_filter(core, 0.35 * K)
glow = nd.gaussian_filter(glow, 1.6 * K)
tint = np.array([0.80, 0.90, 1.00], np.float32)      # the original's pale blue-white light
light = (core[..., None] * 1.0 + glow[..., None] * 0.9) * tint
img = 255 - (255 - img) * (1 - np.clip(light, 0, 255) / 255)   # screen blend

# ---- 6. fine grain, like the original's subtle texture
img += rng.normal(0, 1.1, img.shape[:2])[..., None]
Image.fromarray(np.clip(img + 0.5, 0, 255).astype(np.uint8)).save(out)
print('wrote', out, WK, 'x', HK)
