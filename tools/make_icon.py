"""Make the PC program icon from the player's own game files: the game's memory card save icon.

    python tools/make_icon.py baserom/_Main.rel build/icon.ico [--preview PNG]

The 32x32 save icon (Sonic with goggles, CI8 with an RGB5A3 palette) is read from _Main.rel,
which tools/extract_iso.py has already extracted from the player's disc and checked by SHA-1, so
its position is fixed. The icon is made on the player's PC like the rest of the build; nothing
from the game is in this repository. configure.py compiles build/icon.ico into the PC program.
Standard library only (runs on the setup's embedded Python).
"""
import struct
import sys
import zlib

REL_SIZE = 2572344          # _Main.rel of GXEE8P rev 0
PAL_OFF, ICON_OFF = 0x1F7420, 0x1F7660  # palette (256 x RGB5A3), icon (32x32 CI8, 8x4 tiles)


def rgb5a3(v):
    if v & 0x8000:  # opaque RGB555
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255)
    a, r, g, b = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17, a << 5 | a << 2 | a >> 1)


def decode_icon(rel):
    pal = [rgb5a3(struct.unpack(">H", rel[PAL_OFF + 2 * i:PAL_OFF + 2 * i + 2])[0]) for i in range(256)]
    px = [[None] * 32 for _ in range(32)]
    p = ICON_OFF
    for ty in range(0, 32, 4):
        for tx in range(0, 32, 8):
            for y in range(4):
                for x in range(8):
                    px[ty + y][tx + x] = pal[rel[p]]
                    p += 1
    return px


def scale(px, n):
    """n times larger, each pixel a sharp n x n block"""
    return [[p for p in row for _ in range(n)] for row in px for _ in range(n)]


def half(px):
    """half size, averaging 2x2 blocks (weighted by alpha)"""
    out = []
    for y in range(0, len(px), 2):
        row = []
        for x in range(0, len(px[0]), 2):
            q = [px[y][x], px[y][x + 1], px[y + 1][x], px[y + 1][x + 1]]
            a = sum(c[3] for c in q)
            row.append(tuple(sum(c[i] * c[3] for c in q) // a if a else 0 for i in range(3)) + (a // 4,))
        out.append(row)
    return out


def png(px):
    h, w = len(px), len(px[0])
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in px)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(images):
    """an .ico of PNG images (Windows Vista and newer read these at every size)"""
    head = struct.pack("<HHH", 0, 1, len(images))
    off = 6 + 16 * len(images)
    dirs, data = b"", b""
    for px in images:
        p = png(px)
        w = len(px[0])
        dirs += struct.pack("<BBBBHHII", w % 256, w % 256, 0, 0, 1, 32, len(p), off + len(data))
        data += p
    return head + dirs + data


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit("usage: make_icon.py _Main.rel OUT.ico [--preview PNG]")
    with open(args[0], "rb") as f:
        rel = f.read()
    if len(rel) != REL_SIZE:
        sys.exit("make_icon: %s is not the GXEE8P rev 0 _Main.rel" % args[0])
    px = decode_icon(rel)
    with open(args[1], "wb") as f:
        f.write(ico([half(px), px, scale(px, 2), scale(px, 4), scale(px, 8)]))  # 16 32 64 128 256
    if "--preview" in sys.argv:
        with open(sys.argv[sys.argv.index("--preview") + 1], "wb") as f:
            f.write(png(scale(px, 4)))
    print("make_icon: wrote %s" % args[1])


if __name__ == "__main__":
    main()
