#!/usr/bin/env python3
"""Verify your Sonic Riders disc image and extract the files the recompiler reads.

  extract_iso.py ISO OUTDIR [--no-verify-iso]

The image must be Sonic Riders (USA) GXEE8P revision 0 (Redump: SHA-1 ce0cecf5...9613). Writes
main.dol, _Main.rel and SonicR.str into OUTDIR (baserom/ by default in the docs; gitignored) and
checks each against its known SHA-1. Nothing is uploaded or shared; never commit these files.
"""
import hashlib
import os
import struct
import sys

ISO_SHA1 = "ce0cecf57034bef8bab7d6f588ce92c85a849613"
FILES = {  # name on disc -> SHA-1 of the extracted file
    "_Main.rel": "1607fbb1ccabf116642c3c59ee9b21c304a1db26",
    "SonicR.str": "caf827eb8c510744c0825304f68ae89250bb11a3",
}
DOL_SHA1 = "e90a13504974b917a675d59f07014e0e817c5e5d"


def fail(msg):
    print("extract_iso: " + msg, file=sys.stderr)
    sys.exit(1)


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        fail("usage: extract_iso.py ISO OUTDIR [--no-verify-iso]")
    iso, out = args
    if not os.path.isfile(iso):
        fail("disc image not found: %s" % iso)
    with open(iso, "rb") as f:
        hdr = f.read(0x440)
        if len(hdr) < 0x440 or hdr[:6] != b"GXEE8P":
            fail("not Sonic Riders (USA): the disc ID is %r, GXEE8P is needed" % hdr[:6])
        if hdr[7] != 0:
            fail("disc revision %d: revision 0 is needed" % hdr[7])
        if "--no-verify-iso" not in sys.argv:
            print("extract_iso: checking the disc image (SHA-1, takes a few seconds)...")
            got = sha1_file(iso)
            if got != ISO_SHA1:
                fail("the disc image's SHA-1 is %s, expected %s (a clean GXEE8P rev 0 dump, "
                     "like Redump's; trimmed/compressed/modified images are not supported)" % (got, ISO_SHA1))
            print("extract_iso: disc image OK (Redump GXEE8P rev 0)")
        dol_off, fst_off, fst_size = struct.unpack(">III", hdr[0x420:0x42C])
        # main.dol: its size from the section table (7 text + 11 data sections)
        f.seek(dol_off)
        dh = f.read(0x100)
        offs = struct.unpack(">18I", dh[0:72])
        sizes = struct.unpack(">18I", dh[0x90:0x90 + 72])
        dol_size = max([0x100] + [o + s for o, s in zip(offs, sizes) if s])
        f.seek(dol_off)
        dol = f.read(dol_size)
        f.seek(fst_off)
        fst = f.read(fst_size)
    os.makedirs(out, exist_ok=True)
    if hashlib.sha1(dol).hexdigest() != DOL_SHA1:
        fail("main.dol does not match (SHA-1 %s)" % hashlib.sha1(dol).hexdigest())
    with open(os.path.join(out, "main.dol"), "wb") as w:
        w.write(dol)
    n = struct.unpack(">I", fst[8:12])[0]
    names = fst[n * 12:]
    found = {}
    for i in range(1, n):
        t, a, b = struct.unpack(">III", fst[i * 12:i * 12 + 12])
        if t >> 24:
            continue
        name = names[t & 0xFFFFFF:].split(b"\0", 1)[0].decode("ascii", "replace")
        if name in FILES:
            found[name] = (a, b)
    with open(iso, "rb") as f:
        for name, want in FILES.items():
            if name not in found:
                fail("%s not found on the disc" % name)
            a, b = found[name]
            f.seek(a)
            data = f.read(b)
            if hashlib.sha1(data).hexdigest() != want:
                fail("%s does not match (SHA-1 %s)" % (name, hashlib.sha1(data).hexdigest()))
            with open(os.path.join(out, name), "wb") as w:
                w.write(data)
    print("extract_iso: wrote main.dol, _Main.rel, SonicR.str to %s (all verified)" % out)


if __name__ == "__main__":
    main()
