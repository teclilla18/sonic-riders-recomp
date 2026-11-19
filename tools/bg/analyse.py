"""Measure every element of the supplied background picture (grid, crossings, streaks, squares, base glow)
and write them to tools/bg/elements.json, so render.py can redraw the picture at any resolution."""
import json, sys
import numpy as np
from PIL import Image

src = sys.argv[1]
a = np.asarray(Image.open(src).convert('RGB')).astype(np.float32)
L = a.mean(axis=2)
H, W = L.shape

def refine(peaks):
    k = np.arange(len(peaks)); p = np.polyfit(k, peaks, 1); return float(p[1]), float(p[0])
# exact grid: least squares through the measured line positions
x0, px = refine([51, 117, 183, 250, 316, 383, 449, 516, 582, 648])
y0, py = refine([342, 408, 475, 541, 608, 674, 741, 807, 873, 940, 1006, 1073, 1139, 1206])
# refine sub-pixel by maximising brightness of the dotted lines
def score(off, pitch, prof, n):
    s = 0
    for k in range(-1, n + 2):
        c = off + k * pitch
        if 2 <= c < len(prof) - 2:
            i = int(round(c)); s += prof[i]
    return s
colp = L[300:1250].mean(axis=0); rowp = L[:, 60:640].mean(axis=1)
best = max(((score(x0 + dx, px + dp, colp, 11), x0 + dx, px + dp) for dx in np.arange(-2, 2.01, .25) for dp in np.arange(-.2, .21, .02)))
x0, px = best[1], best[2]
best = max(((score(y0 + dy - 5 * py, py + dp, rowp, 25), y0 + dy, py + dp) for dy in np.arange(-2, 2.01, .25) for dp in np.arange(-.2, .21, .02)))
y0, py = best[1], best[2]
cols = [x0 + k * px for k in range(-1, 12) if -px < x0 + k * px < W + px]
rows = [y0 + k * py for k in range(-6, 22) if -py < y0 + k * py < H + py]
print('grid x0 %.2f px %.3f  y0 %.2f py %.3f  cols %d rows %d' % (x0, px, y0, py, len(cols), len(rows)))

def at(x, y):
    xi, yi = int(round(x)), int(round(y))
    return L[min(max(yi, 0), H - 1), min(max(xi, 0), W - 1)]

# dotted-line dot spacing: autocorrelation along a quiet line
seg = L[int(round(rows[10])), :].copy(); seg -= seg.mean()
ac = [float((seg[:-d] * seg[d:]).mean()) for d in range(2, 12)]
dot_step = 2 + int(np.argmax(ac))
print('dot step ~', dot_step, [round(v, 1) for v in ac])

# background field: median of each cell's interior (lines excluded) on a coarse grid
bg = np.zeros((len(rows) + 1, len(cols) + 1, 3))
cells = []
for j in range(len(rows) - 1):
    for i in range(len(cols) - 1):
        xa, xb, ya, yb = cols[i] + 6, cols[i + 1] - 6, rows[j] + 6, rows[j + 1] - 6
        xa, xb, ya, yb = int(max(xa, 0)), int(min(xb, W)), int(max(ya, 0)), int(min(yb, H))
        if xb - xa < 8 or yb - ya < 8:
            continue
        patch = a[ya:yb, xa:xb].reshape(-1, 3)
        cells.append(dict(i=i, j=j, mean=patch.mean(0).tolist(), med=np.median(patch, 0).tolist(), area=(xb - xa) * (yb - ya)))
# a cell is a "square" if clearly brighter than the median of its neighbourhood
meds = {(c['i'], c['j']): np.array(c['med']) for c in cells}
squares = []
for c in cells:
    nb = [meds[k] for k in [(c['i'] + di, c['j'] + dj) for di in (-1, 0, 1) for dj in (-2, -1, 0, 1, 2)] if k in meds and k != (c['i'], c['j'])]
    base = np.median(nb, 0)
    diff = np.array(c['mean']) - base
    if diff.mean() > 3.0:
        squares.append(dict(i=c['i'], j=c['j'], rgb=[round(v, 1) for v in c['mean']], base=[round(v, 1) for v in base]))
print('squares', len(squares))

# crossings
cross = []
line_level = np.median([at(cols[i], rows[j] + py / 2) for i in range(1, len(cols) - 1) for j in range(1, len(rows) - 1)])
for j, y in enumerate(rows):
    for i, x in enumerate(cols):
        if not (2 <= x < W - 2 and 2 <= y < H - 2):
            continue
        xi, yi = int(round(x)), int(round(y))
        p = L[max(yi - 7, 0):yi + 8, max(xi - 7, 0):xi + 8]
        c = float(L[yi - 1:yi + 2, xi - 1:xi + 2].max())
        blob = int((L[max(yi - 4, 0):yi + 5, max(xi - 4, 0):xi + 5] > 140).sum())
        arms_h = float(np.mean([L[yi, xi + d] for d in (-7, -6, 6, 7) if 0 <= xi + d < W]))
        arms_v = float(np.mean([L[yi + d, xi] for d in (-7, -6, 6, 7) if 0 <= yi + d < H]))
        diag = float(np.mean([L[yi + d, xi + d] for d in (-4, 4) if 0 <= yi + d < H and 0 <= xi + d < W]))
        d2 = float(np.mean([L[yi + dy, xi + dx] for dx in (-2, 2) for dy in (-2, 2) if 0 <= yi + dy < H and 0 <= xi + dx < W]))
        rgb = a[yi - 1:yi + 2, xi - 1:xi + 2].reshape(-1, 3).max(0).tolist()
        cross.append(dict(i=i, j=j, x=x, y=y, c=c, blob=blob, ah=arms_h, av=arms_v, diag=diag, d2=d2, rgb=rgb))

# streaks along rows and columns: brightness on the line, smoothed, against the quiet dotted level
def runs(profile, thr, minlen, tail=46):
    out, s = [], None
    for k, v in enumerate(profile):
        if v > thr and s is None: s = k
        if (v <= thr or k == len(profile) - 1) and s is not None:
            if k - s >= minlen:
                a, b = s, k   # follow the fading tails down to just above the dotted line's level
                while a > 0 and profile[a - 1] > tail: a -= 1
                while b < len(profile) and profile[b] > tail: b += 1
                if out and a <= out[-1][1]: out[-1] = (out[-1][0], b)
                else: out.append((a, b))
            s = None
    return out
def smooth(v, r=3):
    k = np.ones(2 * r + 1) / (2 * r + 1); return np.convolve(v, k, mode='same')
streaks = []
for j, y in enumerate(rows):
    if not 1 <= y < H - 1: continue
    yi = int(round(y)); prof = smooth(L[yi - 1:yi + 2, :].max(0))
    for s, e in runs(prof, 70, 14):
        streaks.append(dict(axis='h', line=j, pos=y, start=s, end=e, profile=[round(float(v), 1) for v in prof[s:e]],
                            rgb=a[yi, s:e].max(0).tolist()))
for i, x in enumerate(cols):
    if not 1 <= x < W - 1: continue
    xi = int(round(x)); prof = smooth(L[:, xi - 1:xi + 2].max(1))
    for s, e in runs(prof, 70, 14):
        streaks.append(dict(axis='v', line=i, pos=x, start=s, end=e, profile=[round(float(v), 1) for v in prof[s:e]],
                            rgb=a[s:e, xi].max(0).tolist()))
print('streaks', len(streaks), 'dotted level', line_level)
json.dump(dict(W=W, H=H, x0=x0, px=px, y0=y0, py=py, cols=cols, rows=rows, dot_step=dot_step, line_level=float(line_level),
               cells=cells, squares=squares, cross=cross, streaks=streaks), open('tools/bg/elements.json', 'w'))
