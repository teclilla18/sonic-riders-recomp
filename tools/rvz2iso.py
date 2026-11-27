"""Convert a GameCube RVZ or WIA disc image (Dolphin's compressed formats) back to a plain 1:1 ISO.

    python -I tools/rvz2iso.py IMAGE.rvz OUT.iso [--sha1 HEX] [--zstd-dir DIR] [--zstd-lib PATH]

The setup runs this when the player picks an .rvz/.wia instead of an .iso, then builds from the ISO
as usual. Everything the formats allow for GameCube discs is handled: compression none, purge (WIA),
bzip2, LZMA, LZMA2 and Zstandard (RVZ's default), any block size, and RVZ's packed junk data (the
disc's padding is regenerated with Nintendo's generator, as Dolphin does). Wii images are refused.

zstd needs Python 3.14's compression.zstd or the 'zstandard' package (--zstd-dir: a folder holding the
unpacked wheel; the Windows setup downloads a pinned one), or else a libzstd shared library loaded with
ctypes (--zstd-lib, or libzstd.so.1 on the library path: the Android no-PC app's way). The rest is the
standard library.

The output is written to OUT.iso.part and renamed when complete. --sha1 checks the result's SHA-1
(computed while writing) and deletes it on a mismatch. Format reference: Dolphin's docs/WiaAndRvz.md.
The input file is only read, never changed.
"""
import argparse
import hashlib
import os
import struct
import sys
import time

BLOCK = 0x8000           # junk data restarts every 32 KiB of the disc (Wii block size in Dolphin)
COMP_NAMES = {0: "none", 1: "purge", 2: "bzip2", 3: "LZMA", 4: "LZMA2", 5: "zstd"}


try:  # optional: a Python without _bz2 / _lzma (a minimal Android build) can still convert zstd images
    import bz2
except ImportError:
    bz2 = None
try:
    import lzma
except ImportError:
    lzma = None


class Bad(Exception):
    pass


class NeedZstd(Bad):
    """a zstd image on a Python without zstd (the setups show their own plain-words message for it)"""


# ---------------------------------------------------------------- junk data (Dolphin LaggedFibonacciGenerator)
LFG_K, LFG_J, SEED_WORDS = 521, 32, 17
BUF_BYTES = LFG_K * 4


class Junk:
    def __init__(self, seed):
        w = list(struct.unpack(">17I", seed))
        for i in range(SEED_WORDS, LFG_K):
            w.append(((w[i - 17] << 23) ^ (w[i - 16] >> 9) ^ w[i - 1]) & 0xFFFFFFFF)
        # the "shift by 18 instead of 16" of the disc's generator, folded into the buffer
        w = [(x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000) for x in w]
        self.buf = bytearray(struct.pack(">521I", *w))
        for _ in range(4):
            self.forward()
        self.pos = 0

    def forward(self):
        b = self.buf
        J = LFG_J * 4
        # words 0..31 ^= words 489..520 (not yet updated), then each word ^= the word 32 before it
        b[0:J] = (int.from_bytes(b[0:J], "big") ^ int.from_bytes(b[BUF_BYTES - J:BUF_BYTES], "big")).to_bytes(J, "big")
        for s in range(J, BUF_BYTES, J):
            e = min(s + J, BUF_BYTES)
            n = e - s
            b[s:e] = (int.from_bytes(b[s:e], "big") ^ int.from_bytes(b[s - J:s - J + n], "big")).to_bytes(n, "big")

    def skip(self, n):
        self.pos += n
        while self.pos >= BUF_BYTES:
            self.forward()
            self.pos -= BUF_BYTES

    def take(self, n):
        out = bytearray()
        while n:
            k = min(n, BUF_BYTES - self.pos)
            out += self.buf[self.pos:self.pos + k]
            self.pos += k
            n -= k
            if self.pos == BUF_BYTES:
                self.forward()
                self.pos = 0
        return out


def rvz_unpack(data, disc_offset, out_len):
    """RVZ packed group: runs of (u32 size, data) or (u32 size | 0x80000000, 68-byte seed) for junk."""
    out = bytearray()
    p = 0
    off = disc_offset
    while len(out) < out_len:
        if p + 4 > len(data):
            raise Bad("packed group ends early")
        size, = struct.unpack_from(">I", data, p)
        p += 4
        if size & 0x80000000:
            size &= 0x7FFFFFFF
            j = Junk(bytes(data[p:p + SEED_WORDS * 4]))
            p += SEED_WORDS * 4
            j.skip(off % BLOCK)
            out += j.take(size)
        else:
            out += data[p:p + size]
            p += size
        off += size
    if len(out) != out_len:
        raise Bad("packed group has the wrong size")
    return out


# ---------------------------------------------------------------- decompression
def make_decompressor(ctype, cdata, zstd_dir, zstd_lib=None):
    if ctype == 0:
        return lambda d, n: d
    if ctype == 1:
        def purge(d, n):  # segments (u32 offset, u32 size, data), then a SHA-1; the rest is zeros
            out = bytearray(n)
            p = 0
            while p < len(d) - 20:
                o, s = struct.unpack_from(">II", d, p)
                p += 8
                out[o:o + s] = d[p:p + s]
                p += s
            return out
        return purge
    if ctype in (2, 3, 4) and (bz2 if ctype == 2 else lzma) is None:
        raise Bad("this Python has no %s support; convert the image to ISO in Dolphin "
                  "(right-click the game > Convert File > ISO)" % COMP_NAMES[ctype])
    if ctype == 2:
        return lambda d, n: bz2.decompress(d)
    if ctype in (3, 4):
        if ctype == 3:
            if len(cdata) < 5:
                raise Bad("LZMA settings missing")
            prop = cdata[0]
            lc, rest = prop % 9, prop // 9
            filt = {"id": lzma.FILTER_LZMA1, "lc": lc, "lp": rest % 5, "pb": rest // 5,
                    "dict_size": struct.unpack_from("<I", cdata, 1)[0]}
        else:
            if len(cdata) < 1:
                raise Bad("LZMA2 settings missing")
            p = cdata[0]
            dsz = 0xFFFFFFFF if p >= 40 else (2 | (p & 1)) << (p // 2 + 11)
            filt = {"id": lzma.FILTER_LZMA2, "dict_size": dsz}
        return lambda d, n: lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[filt]).decompress(d)
    if ctype == 5:
        try:
            from compression import zstd as z  # Python 3.14+
            return lambda d, n: z.ZstdDecompressor().decompress(d)
        except ImportError:
            pass
        if zstd_dir:
            sys.path.insert(0, zstd_dir)
        try:
            import zstandard
            return lambda d, n: zstandard.ZstdDecompressor().decompressobj().decompress(d)
        except ImportError:
            pass
        dec = libzstd_decompressor(zstd_lib)
        if dec:
            return dec
        raise NeedZstd("this image uses zstd compression, which needs Python 3.14+ or the 'zstandard' package "
                       "(python3 -m pip install zstandard); or convert it to ISO in Dolphin "
                       "(right-click the game > Convert File > ISO)")
    raise Bad("unknown compression type %d" % ctype)


def libzstd_decompressor(path=None):
    """zstd through the C library with ctypes (the Android no-PC app ships libzstd.so.1 for clang and has
    Python 3.11, so neither compression.zstd nor the zstandard wheel is there). None if not loadable."""
    try:
        import ctypes
        import ctypes.util
    except ImportError:
        return None
    names = [path] if path else ["libzstd.so.1", ctypes.util.find_library("zstd"), "libzstd.dylib", "libzstd.dll"]
    lib = None
    for name in names:
        if not name:
            continue
        try:
            lib = ctypes.CDLL(name)
            break
        except OSError:
            continue
    if lib is None:
        return None

    class Buf(ctypes.Structure):  # ZSTD_inBuffer / ZSTD_outBuffer have the same layout
        _fields_ = [("ptr", ctypes.c_void_p), ("size", ctypes.c_size_t), ("pos", ctypes.c_size_t)]

    size_t = ctypes.c_size_t
    lib.ZSTD_createDCtx.restype = ctypes.c_void_p
    lib.ZSTD_DCtx_reset.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.ZSTD_DCtx_reset.restype = size_t
    lib.ZSTD_decompressStream.argtypes = [ctypes.c_void_p, ctypes.POINTER(Buf), ctypes.POINTER(Buf)]
    lib.ZSTD_decompressStream.restype = size_t
    lib.ZSTD_isError.argtypes = [size_t]
    lib.ZSTD_isError.restype = ctypes.c_uint
    lib.ZSTD_getErrorName.argtypes = [size_t]
    lib.ZSTD_getErrorName.restype = ctypes.c_char_p
    dctx = lib.ZSTD_createDCtx()
    if not dctx:
        return None
    cap = 1 << 20
    out_mem = ctypes.create_string_buffer(cap)

    def check(code):
        if lib.ZSTD_isError(code):
            raise Bad("zstd: %s" % lib.ZSTD_getErrorName(code).decode("ascii", "replace"))
        return code

    def dec(d, n):
        d = bytes(d)
        src = ctypes.create_string_buffer(d, len(d))
        inb = Buf(ctypes.cast(src, ctypes.c_void_p), len(d), 0)
        check(lib.ZSTD_DCtx_reset(dctx, 1))  # ZSTD_reset_session_only
        parts = []
        while True:
            outb = Buf(ctypes.cast(out_mem, ctypes.c_void_p), cap, 0)
            left = check(lib.ZSTD_decompressStream(dctx, ctypes.byref(outb), ctypes.byref(inb)))
            parts.append(ctypes.string_at(out_mem, outb.pos))
            if inb.pos == inb.size:
                if left == 0:  # frame complete and flushed (calling again would start a new frame)
                    return b"".join(parts)
                if outb.pos < cap:
                    raise Bad("zstd: the compressed data ends early (damaged image?)")

    return dec


# ---------------------------------------------------------------- the image
def convert(src, dst, want_sha1=None, zstd_dir=None, progress=True, zstd_lib=None):
    f = open(src, "rb")
    h1 = f.read(0x48)
    if len(h1) < 0x48 or h1[:4] not in (b"RVZ\x01", b"WIA\x01"):
        raise Bad("not an RVZ or WIA image")
    rvz = h1[:4] == b"RVZ\x01"
    h2_size, = struct.unpack_from(">I", h1, 12)
    iso_size, = struct.unpack_from(">Q", h1, 0x24)
    h2 = f.read(h2_size)
    if len(h2) < 0xDC:
        raise Bad("header too short (file incomplete?)")
    disc_type, ctype, _level, chunk = struct.unpack_from(">IIiI", h2, 0)
    dhead = h2[0x10:0x90]
    n_part, = struct.unpack_from(">I", h2, 0x90)
    n_raw, raw_off, raw_size = struct.unpack_from(">IQI", h2, 0xB4)
    n_grp, grp_off, grp_size = struct.unpack_from(">IQI", h2, 0xC4)
    cdata = h2[0xD5:0xD5 + h2[0xD4]]
    if disc_type != 1 or n_part:
        raise Bad("this is a Wii image; only GameCube discs are supported")
    if chunk == 0 or chunk % BLOCK:
        raise Bad("unusual block size %d" % chunk)
    dec = make_decompressor(ctype, cdata, zstd_dir, zstd_lib)
    fsize = os.fstat(f.fileno()).st_size

    def read_at(off, n):
        if off + n > fsize:
            raise Bad("the image file is incomplete (truncated download or copy?)")
        f.seek(off)
        return f.read(n)

    raw_ent = dec(read_at(raw_off, raw_size), n_raw * 24)
    gsz = 12 if rvz else 8
    grp_ent = dec(read_at(grp_off, grp_size), n_grp * gsz)
    if len(raw_ent) < n_raw * 24 or len(grp_ent) < n_grp * gsz:
        raise Bad("entry tables are damaged")
    raws = sorted(struct.unpack_from(">QQII", raw_ent, i * 24) for i in range(n_raw))

    def group(i, disc_off, n):
        if i >= n_grp:
            raise Bad("group %d out of range" % i)
        if rvz:
            o4, dsize, packed = struct.unpack_from(">III", grp_ent, i * 12)
            compressed = dsize >> 31
            dsize &= 0x7FFFFFFF
        else:
            o4, dsize = struct.unpack_from(">II", grp_ent, i * 8)
            packed, compressed = 0, 1
        if dsize == 0:
            return bytes(n)
        d = read_at(o4 * 4, dsize)
        if compressed:
            d = dec(d, packed or n)
        if packed:
            d = rvz_unpack(d, disc_off, n)
        if len(d) != n:
            raise Bad("group %d decompressed to %d bytes, expected %d" % (i, len(d), n))
        return d

    print("Converting %s (%s, %s, %d KiB blocks) to ISO..." % (os.path.basename(src), "RVZ" if rvz else "WIA",
          COMP_NAMES.get(ctype, "?"), chunk // 1024), flush=True)
    part = dst + ".part"
    sha = hashlib.sha1()
    t0 = time.time()
    last = [t0]
    with open(part, "wb") as out:
        pos = [0]

        def emit(b):
            out.write(b)
            sha.update(b)
            pos[0] += len(b)
            now = time.time()
            if progress and now - last[0] >= 5:
                last[0] = now
                print("  %3d%% (%d MB)" % (pos[0] * 100 // iso_size, pos[0] >> 20), flush=True)

        def emit_range(b, start):  # b holds disc bytes from `start`; write what lies past pos
            if start + len(b) <= pos[0]:
                return
            if start > pos[0]:
                fill(start)
            emit(b[pos[0] - start:])

        def fill(upto):
            while pos[0] < upto:
                emit(bytes(min(upto - pos[0], 1 << 20)))

        emit(dhead)
        for off, size, gi, ng in raws:
            end = min(off + size, iso_size)
            start = off - off % BLOCK  # entries start at a 32 KiB boundary inside their first group
            for g in range(ng):
                gs = start + g * chunk
                if gs >= end:
                    break
                d = group(gi + g, gs, min(chunk, end - gs))
                lo = max(off, gs)
                emit_range(memoryview(d)[lo - gs:], lo)
        fill(iso_size)
        if pos[0] != iso_size:
            raise Bad("output size %d differs from the ISO size %d" % (pos[0], iso_size))
    f.close()
    digest = sha.hexdigest()
    print("Done in %.0f s: %d bytes, SHA-1 %s" % (time.time() - t0, iso_size, digest), flush=True)
    if want_sha1 and digest != want_sha1.lower():
        os.remove(part)
        raise Bad("the converted image does not match the known-good dump (SHA-1 %s). The RVZ was probably "
                  "made with 'Remove Junk Data' or from a modified/trimmed dump; a 1:1 dump is needed." % digest)
    os.replace(part, dst)
    return digest


def is_compressed_image(path):
    with open(path, "rb") as f:
        return f.read(4) in (b"RVZ\x01", b"WIA\x01")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--sha1", help="expected SHA-1 of the ISO (fail and delete it on a mismatch)")
    ap.add_argument("--zstd-dir", help="folder holding the unpacked 'zstandard' package")
    ap.add_argument("--zstd-lib", help="path of a libzstd shared library (used through ctypes when there is no "
                    "compression.zstd or zstandard)")
    a = ap.parse_args()
    if os.path.abspath(a.src) == os.path.abspath(a.dst):
        sys.exit("error: the output must be a different file")
    try:
        convert(a.src, a.dst, a.sha1, a.zstd_dir, zstd_lib=a.zstd_lib)
    except (Bad, OSError, ValueError) + ((lzma.LZMAError,) if lzma else ()) as e:
        try:
            os.remove(a.dst + ".part")
        except OSError:
            pass
        sys.exit("error: %s" % e)


if __name__ == "__main__":
    main()
