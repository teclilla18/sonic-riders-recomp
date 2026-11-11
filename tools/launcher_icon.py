"""Draw the launcher's program icon (our own drawing, nothing from the game): a gold ring with speed lines on
the launcher's blue. Writes an .ico with 256, 48, 32 and 16 pixel images.

    python tools/launcher_icon.py build/launcher/launcher.ico [--preview PNG]

Standard library only; tools/build_launcher.py runs it.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_icon import ico, png  # noqa: E402

SS = 4  # samples per pixel along each axis


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def rounded_box(u, v, m, r):
    """inside a square with margin m and corner radius r (all in 0..1 units)"""
    x = min(max(u, m + r), 1 - m - r)
    y = min(max(v, m + r), 1 - m - r)
    return (u - x) ** 2 + (v - y) ** 2 <= r * r and m <= u <= 1 - m and m <= v <= 1 - m


def sample(u, v):
    """colour (r, g, b) and coverage 0/1 at a point of the icon"""
    if not rounded_box(u, v, 0.03, 0.2):
        return (0, 0, 0), 0.0
    c = mix((44, 128, 230), (14, 26, 74), v)  # the launcher's banner blue, darker at the bottom
    # speed lines, fading to the left
    for y, x0 in ((0.36, 0.20), (0.50, 0.10), (0.64, 0.20)):
        if (x0 <= u <= 0.46 and abs(v - y) <= 0.035) or math.hypot(u - x0, v - y) <= 0.035:
            c = mix(c, (255, 255, 255), 0.35 + 0.65 * max(0.0, u - x0) / (0.46 - x0))
    # the ring
    d = math.hypot(u - 0.62, v - 0.5)
    if 0.165 <= d <= 0.27:
        t = (d - 0.165) / 0.105
        gold = mix((255, 236, 120), (214, 140, 16), t)
        light = max(0.0, ((0.62 - u) + (0.5 - v)) / (d * math.sqrt(2)))  # 1 towards the upper left
        gold = mix(gold, (255, 250, 210), 0.4 * light * light * (1 - t))
        c = gold
    return c, 1.0


def draw(n):
    px = []
    for j in range(n):
        row = []
        for i in range(n):
            r = g = b = a = 0.0
            for sj in range(SS):
                for si in range(SS):
                    (cr, cg, cb), cov = sample((i + (si + 0.5) / SS) / n, (j + (sj + 0.5) / SS) / n)
                    r += cr * cov
                    g += cg * cov
                    b += cb * cov
                    a += cov
            k = SS * SS
            row.append((int(r / a + 0.5), int(g / a + 0.5), int(b / a + 0.5), int(255 * a / k + 0.5)) if a else (0, 0, 0, 0))
        px.append(row)
    return px


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    images = [draw(n) for n in (256, 48, 32, 16)]
    with open(sys.argv[1], "wb") as f:
        f.write(ico(images))
    if "--preview" in sys.argv:
        with open(sys.argv[sys.argv.index("--preview") + 1], "wb") as f:
            f.write(png(images[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
