#!/usr/bin/env python3
"""List or extract the files of your own Sonic Riders disc image (for making mods).

  disc_files.py list    IMAGE.iso [PATTERN]          names and sizes (PATTERN: e.g. "PS*", "*.ADX")
  disc_files.py extract IMAGE.iso OUTDIR [PATTERN]   copies the matching files into OUTDIR

Reads the disc's file system table (FST); nothing is changed in the image. The files are SEGA's:
use them to make your own mods, never upload or share them. See docs/MODDING.md.
"""
import fnmatch
import os
import struct
import sys


def fail(msg):
    print("disc_files: " + msg, file=sys.stderr)
    sys.exit(1)


def read_fst(f):
    f.seek(0)
    head = f.read(0x440)
    if struct.unpack_from(">I", head, 0x1C)[0] != 0xC2339F3D:
        fail("not a GameCube disc image")
    fst_off, fst_size = struct.unpack_from(">II", head, 0x424)
    f.seek(fst_off)
    fst = f.read(fst_size)
    n = struct.unpack_from(">I", fst, 8)[0]
    names = 12 * n
    out = []

    def name(i):
        o = names + (struct.unpack_from(">I", fst, i * 12)[0] & 0xFFFFFF)
        return fst[o:fst.index(b"\0", o)].decode("latin-1")

    def walk(first, stop, prefix):
        i = first
        while i < stop:
            flags = fst[i * 12]
            nm = name(i)
            if flags:
                nxt = struct.unpack_from(">I", fst, i * 12 + 8)[0]
                walk(i + 1, nxt, prefix + nm + "/")
                i = nxt
            else:
                off, size = struct.unpack_from(">II", fst, i * 12 + 4)
                out.append((prefix + nm, off, size))
                i += 1

    walk(1, n, "")
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 2 or a[0] not in ("list", "extract"):
        fail(__doc__.strip().splitlines()[2] + "\n" + __doc__.strip().splitlines()[3])
    with open(a[1], "rb") as f:
        files = read_fst(f)
        if a[0] == "list":
            pat = a[2] if len(a) > 2 else "*"
            sel = [x for x in files if fnmatch.fnmatch(x[0].upper(), pat.upper())]
            for p, _, size in sel:
                print("%-24s %10d" % (p, size))
            print("%d files" % len(sel))
            return
        if len(a) < 3:
            fail("extract needs an output folder")
        out, pat = a[2], (a[3] if len(a) > 3 else "*")
        sel = [x for x in files if fnmatch.fnmatch(x[0].upper(), pat.upper())]
        for p, off, size in sel:
            dst = os.path.join(out, *p.split("/"))
            os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
            f.seek(off)
            with open(dst, "wb") as w:
                w.write(f.read(size))
        print("extracted %d files to %s" % (len(sel), out))


if __name__ == "__main__":
    main()
