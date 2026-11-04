#!/usr/bin/env python3
"""The Android app icon, made on the player's PC at build time (like the PC icon, tools/make_icon.py).

    python tools/android_icon.py SOURCE RES_DIR [--preview PNG]

SOURCE is one of:
  - baserom/_Main.rel: the game's own memory card save icon from the player's disc (the default; nothing
    from the game is in this repository);
  - an .ico (PNG or 32-bit BMP images; the largest is used) or a .png: a picture of your choice
    (configure.py --icon, setup -Icon). Keep it out of git unless it is your own art.

Writes an adaptive icon (Android 8+): RES_DIR/mipmap-anydpi-v26/ic_launcher.xml with a foreground picture
(the icon filling the 72 dp the launcher shows of the 108 dp canvas) and a background of the icon's edge
colour, so the launcher's mask (circle, squircle) cuts into the background, not the picture. aapt2
compiles RES_DIR into the APK (tools/android_build.py). Standard library only.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import embed_icons  # noqa: E402  (PNG decoder)
import make_icon  # noqa: E402  (save icon decoder, PNG writer)

CANVAS = 432  # 108 dp at xxxhdpi
VIEW = 288    # the 72 dp the launcher shows


def from_ico(data):
    _, kind, count = struct.unpack("<HHH", data[:6])
    if kind != 1 or not count:
        raise ValueError("not an .ico")
    best = None
    for i in range(count):
        w, h, _, _, _, bpp, size, off = struct.unpack("<BBBBHHII", data[6 + 16 * i:22 + 16 * i])
        w, h = w or 256, h or 256
        if best is None or w * h > best[0] * best[1]:
            best = (w, h, bpp, data[off:off + size])
    w, h, bpp, img = best
    if img[:8] == b"\x89PNG\r\n\x1a\n":
        return from_png(img)
    hsize, bw, bh2, _, bits = struct.unpack("<IiiHH", img[:16])
    if bits != 32:
        raise ValueError("only 32-bit .ico images are supported (this one has %d bits per pixel)" % bits)
    bh = bh2 // 2
    p = hsize
    rows = []
    for _ in range(bh):
        row = []
        for _ in range(bw):
            b, g, r, a = img[p:p + 4]
            p += 4
            row.append((r, g, b, a))
        rows.append(row)
    rows.reverse()  # bottom-up
    if all(px[3] == 0 for row in rows for px in row):  # alpha unused: everything opaque
        rows = [[(r, g, b, 255) for r, g, b, _ in row] for row in rows]
    return rows


def from_png(data):
    w, h, rgba = embed_icons.decode_png(data, premultiply=False)
    return [[tuple(rgba[(y * w + x) * 4:(y * w + x) * 4 + 4]) for x in range(w)] for y in range(h)]


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) == make_icon.REL_SIZE:
        px = make_icon.decode_icon(data)
        return [row[2:-2] for row in px[2:-2]]  # without the save icon's 2-pixel frame (the mask cuts corners)
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return from_png(data)
    return from_ico(data)


def resample(px, n):
    """n x n by area averaging (alpha-weighted); integer enlargements stay sharp"""
    h, w = len(px), len(px[0])
    out = []
    for oy in range(n):
        y0, y1 = oy * h / n, (oy + 1) * h / n
        row = []
        for ox in range(n):
            x0, x1 = ox * w / n, (ox + 1) * w / n
            acc = [0.0, 0.0, 0.0, 0.0]
            area = 0.0
            for sy in range(int(y0), min(h, int(y1 + 0.999999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                if wy <= 0:
                    continue
                for sx in range(int(x0), min(w, int(x1 + 0.999999))):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    if wx <= 0:
                        continue
                    r, g, b, a = px[sy][sx]
                    k = wx * wy
                    acc[0] += r * a * k
                    acc[1] += g * a * k
                    acc[2] += b * a * k
                    acc[3] += a * k
                    area += k
            a = acc[3] / area if area else 0
            row.append(tuple(int(round(c / acc[3])) if acc[3] else 0 for c in acc[:3]) + (int(round(a)),))
        out.append(row)
    return out


def edge_colour(px):
    edge = px[0] + px[-1] + [row[0] for row in px] + [row[-1] for row in px]
    edge = [p for p in edge if p[3] > 128] or [(16, 40, 122, 255)]
    return tuple(sum(p[i] for p in edge) // len(edge) for i in range(3)) + (255,)


def make_res(src, res):
    px = load(src)
    if not px or not px[0]:
        raise ValueError("empty picture")
    size = max(len(px), len(px[0]))
    if len(px) != len(px[0]):  # not square: centred on a transparent square
        pad = [(0, 0, 0, 0)] * size
        top = (size - len(px)) // 2
        left = (size - len(px[0])) // 2
        px = [pad[:] for _ in range(top)] + [pad[:left] + row + pad[:size - left - len(row)] for row in px] + \
             [pad[:] for _ in range(size - top - len(px))]
    k = VIEW // size
    if k >= 2:  # small pixel art: whole-pixel blocks (sharp), on transparent edges that show the background
        big = make_icon.scale(px, k)
        e = (VIEW - size * k) // 2
        clear = (0, 0, 0, 0)
        view = [[clear] * VIEW for _ in range(e)] + [[clear] * e + row + [clear] * (VIEW - e - len(row)) for row in big]
        view += [[clear] * VIEW for _ in range(VIEW - len(view))]
    else:
        view = resample(px, VIEW)
    m = (CANVAS - VIEW) // 2
    clear = (0, 0, 0, 0)
    fg = [[clear] * CANVAS for _ in range(m)] + [[clear] * m + row + [clear] * m for row in view] + \
         [[clear] * CANVAS for _ in range(m)]
    bg = [[edge_colour(px)] * 16 for _ in range(16)]
    os.makedirs(os.path.join(res, "drawable-nodpi"), exist_ok=True)
    os.makedirs(os.path.join(res, "mipmap-anydpi-v26"), exist_ok=True)
    with open(os.path.join(res, "drawable-nodpi", "ic_launcher_fg.png"), "wb") as f:
        f.write(make_icon.png(fg))
    with open(os.path.join(res, "drawable-nodpi", "ic_launcher_bg.png"), "wb") as f:
        f.write(make_icon.png(bg))
    with open(os.path.join(res, "mipmap-anydpi-v26", "ic_launcher.xml"), "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@drawable/ic_launcher_bg" />\n'
                '    <foreground android:drawable="@drawable/ic_launcher_fg" />\n'
                '</adaptive-icon>\n')
    return view


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    try:
        view = make_res(args[0], args[1])
    except (OSError, ValueError) as e:
        sys.exit("android_icon: %s: %s" % (args[0], e))
    if "--preview" in sys.argv:
        with open(sys.argv[sys.argv.index("--preview") + 1], "wb") as f:
            f.write(make_icon.png(view))
    print("android_icon: icon from %s" % os.path.basename(args[0]))


if __name__ == "__main__":
    main()
