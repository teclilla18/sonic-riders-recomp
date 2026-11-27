"""rvz2iso: a synthetic zstd RVZ (no game data) converted through the ctypes libzstd path.

    python -I tools/test_rvz2iso.py

Skips when no libzstd shared library can be loaded. The Android no-PC app takes this path (Python 3.11,
libzstd.so.1 from its tool bundle).
"""
import ctypes
import ctypes.util
import hashlib
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rvz2iso  # noqa: E402

CHUNK = 2 * 1024 * 1024  # bigger than the decompressor's 1 MiB output buffer: several passes per group


def load_libzstd():
    for name in ("libzstd.so.1", ctypes.util.find_library("zstd")):
        if name:
            try:
                return ctypes.CDLL(name), name
            except OSError:
                pass
    return None, None


LIB, LIB_NAME = load_libzstd()


def zstd_compress(data):
    LIB.ZSTD_compressBound.restype = ctypes.c_size_t
    LIB.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
    LIB.ZSTD_compress.restype = ctypes.c_size_t
    LIB.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
    cap = LIB.ZSTD_compressBound(len(data))
    out = ctypes.create_string_buffer(cap)
    n = LIB.ZSTD_compress(out, cap, data, len(data), 3)
    return out.raw[:n]


def make_disc(size):
    """Fake GameCube-shaped bytes: a header, some patterned data, a zero stretch (a dsize-0 group)."""
    disc = bytearray(size)
    disc[0:6] = b"TEST01"
    disc[0x1C:0x20] = struct.pack(">I", 0xC2339F3D)  # GameCube magic
    for i in range(0x80, CHUNK + CHUNK // 2, 4096):
        disc[i:i + 32] = hashlib.sha256(i.to_bytes(8, "big")).digest()
    return bytes(disc)  # groups from 2 to the end stay zero


def make_rvz(disc, path, zero_groups=()):
    iso_size = len(disc)
    n_grp = (iso_size + CHUNK - 1) // CHUNK
    body = bytearray()
    entries = bytearray()
    for g in range(n_grp):
        data = disc[g * CHUNK:(g + 1) * CHUNK]
        if g in zero_groups:
            entries += struct.pack(">III", 0, 0, 0)
            continue
        comp = zstd_compress(data)
        entries += struct.pack(">III", 0, len(comp) | 0x80000000, 0)  # offset patched below
        body += comp
        body += bytes(-len(body) % 4)
    raws = zstd_compress(struct.pack(">QQII", 0x80, iso_size - 0x80, 0, n_grp))
    grps_plain_len = len(entries)
    head_len = 0x48 + 0xDC
    raw_off = head_len
    grp_off = raw_off + len(raws) + (-len(raws) % 4)
    data_off = grp_off + 4 * ((grps_plain_len * 2 + 64) // 4)  # room for the compressed group table
    pos = data_off
    fixed = bytearray()
    for g in range(n_grp):
        o4, dsize, packed = struct.unpack_from(">III", entries, g * 12)
        if dsize:
            fixed += struct.pack(">III", pos // 4, dsize, packed)
            n = dsize & 0x7FFFFFFF
            pos += n + (-n % 4)
        else:
            fixed += struct.pack(">III", 0, 0, 0)
    grps = zstd_compress(bytes(fixed))
    assert grp_off + len(grps) <= data_off
    h2 = bytearray(0xDC)
    struct.pack_into(">IIiI", h2, 0, 1, 5, 3, CHUNK)
    h2[0x10:0x90] = disc[:0x80]
    struct.pack_into(">IQI", h2, 0xB4, 1, raw_off, len(raws))
    struct.pack_into(">IQI", h2, 0xC4, n_grp, grp_off, len(grps))
    h1 = bytearray(0x48)
    h1[0:4] = b"RVZ\x01"
    struct.pack_into(">I", h1, 12, len(h2))
    struct.pack_into(">Q", h1, 0x24, iso_size)
    with open(path, "wb") as f:
        f.write(h1 + h2)
        f.write(raws + bytes(grp_off - raw_off - len(raws)))
        f.write(grps + bytes(data_off - grp_off - len(grps)))
        f.write(body)


@unittest.skipIf(LIB is None, "no libzstd shared library here")
class LibzstdPath(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.src = os.path.join(self.dir.name, "t.rvz")
        self.dst = os.path.join(self.dir.name, "t.iso")
        self.disc = make_disc(5 * CHUNK + 12345)
        make_rvz(self.disc, self.src, zero_groups=(3,))

    def tearDown(self):
        self.dir.cleanup()

    def test_ctypes_decompressor_round_trip(self):
        dec = rvz2iso.libzstd_decompressor(LIB_NAME)
        self.assertIsNotNone(dec)
        data = os.urandom(3000) * 1000  # 3 MB, several output passes
        self.assertEqual(dec(zstd_compress(data), len(data)), data)
        self.assertEqual(dec(zstd_compress(b"x" * 10), 10), b"x" * 10)  # the context is reused

    def test_truncated_data_fails(self):
        dec = rvz2iso.libzstd_decompressor(LIB_NAME)
        comp = zstd_compress(os.urandom(200000))
        with self.assertRaises(rvz2iso.Bad):
            dec(comp[:len(comp) // 2], 200000)

    def test_convert_through_ctypes(self):
        # force the ctypes path even where compression.zstd or zstandard exist
        real = rvz2iso.make_decompressor

        def only_ctypes(ctype, cdata, zstd_dir, zstd_lib=None):
            return rvz2iso.libzstd_decompressor(zstd_lib) if ctype == 5 else real(ctype, cdata, zstd_dir, zstd_lib)

        rvz2iso.make_decompressor = only_ctypes
        try:
            want = hashlib.sha1(self.disc).hexdigest()
            got = rvz2iso.convert(self.src, self.dst, want, progress=False, zstd_lib=LIB_NAME)
        finally:
            rvz2iso.make_decompressor = real
        self.assertEqual(got, want)
        with open(self.dst, "rb") as f:
            self.assertEqual(f.read(), self.disc)

    def test_wrong_sha1_deletes_output(self):
        with self.assertRaises(rvz2iso.Bad):
            rvz2iso.convert(self.src, self.dst, "0" * 40, progress=False, zstd_lib=LIB_NAME)
        self.assertFalse(os.path.exists(self.dst) or os.path.exists(self.dst + ".part"))


if __name__ == "__main__":
    unittest.main()
