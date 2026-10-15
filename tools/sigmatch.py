#!/usr/bin/env python3
"""Name DOL functions with Dolphin's hash signature database (Data/Sys/totaldb.dsy).

The checksum is a port of Dolphin's HashSignatureDB::ComputeCodeChecksum: it hashes opcode
fields only (operands that relocation changes are masked), so the same SDK function matches
across games. Function boundaries come from decomp-toolkit's symbols.txt.

usage: sigmatch.py baserom/main.dol config/GXEE8P/symbols.txt build/ext/totaldb.dsy OUT.txt
OUT.txt: one line per matched function: ADDR SIZE NAME [size-mismatch] [ambiguous]
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gcbin import Dol, read_symbols


def checksum(words):
    s = 0
    for w in words:
        op = w & 0xFC000000
        op2 = op3 = 0
        aux = op >> 26
        if aux == 4:
            op2 = w & 0x3F
            if op2 in (0, 8, 16, 21, 22):
                op3 = w & 0x7C0
        elif aux in (7, 8, 10, 11, 12, 13, 14, 15):
            op2 = w & 0x03FF0000
        elif aux in (19, 31, 63):
            op2 = w & 0x7FF
        elif aux == 59:
            op2 = w & 0x3F
            if op2 < 16:
                op3 = w & 0x7C0
        elif 32 <= aux < 56:
            op2 = w & 0x03FF0000
        s = ((s << 17) & 0xFFFE0000) | ((s >> 15) & 0x0001FFFF)
        s ^= op | op2 | op3
    return s


def load_dsy(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<I", d, 0)[0]
    db = collections.defaultdict(list)
    for i in range(n):
        cs, size, name = struct.unpack_from("<II128s", d, 4 + 136 * i)
        db[cs].append((name.split(b"\0")[0].decode("latin-1"), size))
    return db


def main():
    dol_path, sym_path, dsy_path, out_path = sys.argv[1:5]
    dol = Dol(dol_path)
    db = load_dsy(dsy_path)
    funcs = [s for s in read_symbols(sym_path) if s["type"] == "function" and s["size"]]
    matched = 0
    lines = []
    for f in funcs:
        words = struct.unpack(">%dI" % (f["size"] // 4), dol.read(f["addr"], f["size"]))
        hits = db.get(checksum(words))
        if not hits:
            continue
        names = sorted(set(n for n, _ in hits))
        size_ok = any(sz == f["size"] for _, sz in hits)
        flags = ("" if size_ok else " size-mismatch") + (" ambiguous:" + ",".join(names[1:]) if len(names) > 1 else "")
        lines.append("%08X %6X %s%s" % (f["addr"], f["size"], names[0], flags))
        matched += 1
    open(out_path, "w").write("\n".join(lines) + "\n")
    print("%d of %d functions matched (%d with exact size)" % (
        matched, len(funcs), sum(1 for l in lines if "size-mismatch" not in l)))


if __name__ == "__main__":
    main()
