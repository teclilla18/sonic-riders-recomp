#!/usr/bin/env python3
"""Sonic Riders HOME-screen icon for the Switch: a forwarder NSP, built on the player's PC.

    python tools/nsp/make_forwarder.py --devkitpro DIR --cc CC --keys prod.keys [--icon PICTURE] [--out FILE]

The NSP holds only a loader (nx-hbloader, patched to start sdmc:/switch/sonicriders/sonicriders.nro and to close
when the game returns), the name "Sonic Riders" and an icon. No game code or data. Opening it from the HOME
screen starts the .nro as an application (full memory, no need to hold R over a game).

- Your console's prod.keys (dumped with Lockpick_RCM) are needed to pack an NSP. They are read where they are by
  hacBrewPack, never copied, and the NSP is yours only: never share it.
- --icon: your own picture (.png or .ico; any size, square looks best; it becomes a 256 x 256 JPEG). Without it,
  the game's memory card icon from your own disc (baserom/_Main.rel) is used.
- --cc: a C compiler for this PC, used once for hacBrewPack (llvm-mingw's clang on Windows, "zig cc" or cc on
  Linux). --devkitpro: devkitPro with devkitA64, libnx and switch-tools (the setup's own).

Sources are downloaded once from GitHub at pinned commits and checked (SHA-256 of their file tree) before use:
nx-hbloader v2.4.5 (ISC license) and hacBrewPack v3.05 (GPL-2.0, used as a tool, not shipped). Standard library
only. Installing: docs/INSTALL.md "Home-screen icon".
"""
import argparse
import hashlib
import io
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import android_icon  # noqa: E402  (.png/.ico/save icon loader, resampling)

TITLE_ID = "010053520d9a0000"   # the same id as the forwarder of 2026-10-18 (tools/nsp/sonicriders.json)
NRO_PATH = "sdmc:/switch/sonicriders/sonicriders.nro"
SOURCES = {
    "nx-hbloader": ("switchbrew/nx-hbloader", "82b95122c5ae8dc059bf23893ba7623c72c86773",
                    "804481d74027c090bab9a64491257988966da69c5a3fee7234880161810b17a8"),
    "hacBrewPack": ("rlaphoenix/hacBrewPack", "745b16ecfc9ce055743067d200572204cb2aac6c",
                    "34807f314187cb65f5dd750cfc628710893f4eb26777313c5ee01226c8c92ecb"),
}


class Error(Exception):
    pass


def log(msg):
    print(msg, flush=True)


# ------------------------------------------------------------------ sources
def tree_sha256(files):
    h = hashlib.sha256()
    for name, data in sorted(files):
        h.update(b"%s\0%d\0" % (name.encode(), len(data)))
        h.update(data)
    return h.hexdigest()


def fetch(work, name, print_hash=False):
    repo, rev, want = SOURCES[name]
    dst = os.path.join(work, "src", name)
    stamp = os.path.join(dst, ".tree-sha256")
    if os.path.isfile(stamp) and open(stamp).read().strip() == want:
        return dst
    url = "https://codeload.github.com/%s/tar.gz/%s" % (repo, rev)
    log("  downloading %s" % url)
    req = urllib.request.Request(url, headers={"User-Agent": "sonicriders-forwarder"})
    data = urllib.request.urlopen(req, timeout=180).read()
    files = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
        for m in t.getmembers():
            if not m.isfile() or "/" not in m.name:
                continue
            rel = m.name.split("/", 1)[1]
            if rel.startswith("/") or ".." in rel.split("/"):
                raise Error("unexpected path in the %s archive: %s" % (name, m.name))
            files.append((rel, t.extractfile(m).read()))
    got = tree_sha256(files)
    if print_hash:
        log("  %s tree SHA-256 %s" % (name, got))
    if got != want:
        raise Error("%s at %s is not the pinned source (tree SHA-256 %s)" % (name, rev, got))
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    for rel, b in files:
        p = os.path.join(dst, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(b)
    with open(stamp, "w") as f:
        f.write(want)
    return dst


def run(cmd, cwd=None, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode:
        raise Error("%s failed:\n%s" % (os.path.basename(cmd[0]), r.stdout[-3000:]))


# ------------------------------------------------------------------ the loader (nx-hbloader)
def patch_hbloader(src):
    p = os.path.join(src, "source", "main.c")
    s = open(p, encoding="utf-8").read()
    if "s_ranOnce" in s:
        return
    a = '#define DEFAULT_NRO "sdmc:/hbmenu.nro"'
    b = "    if (g_nextNroPath[0] == '\\0')\n    {\n"
    if a not in s or s.count(b) != 1:
        raise Error("nx-hbloader's main.c is not the expected version")
    s = s.replace(a, '#define DEFAULT_NRO "%s"' % NRO_PATH)
    s = s.replace(b, "    static bool s_ranOnce;\n" + b +
                  "        /* forwarder: when the game returns, close instead of loading it again */\n"
                  "        if (s_ranOnce)\n            svcExitProcess();\n        s_ranOnce = true;\n")
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(s)


def build_hbloader(work, dkp):
    src = fetch(work, "nx-hbloader")
    patch_hbloader(src)
    exe = ".exe" if os.name == "nt" else ""
    gcc = os.path.join(dkp, "devkitA64", "bin", "aarch64-none-elf-gcc" + exe)
    tools = os.path.join(dkp, "tools", "bin")
    if not os.path.isfile(gcc):
        raise Error("devkitA64 not found in %s" % dkp)
    out = os.path.join(work, "hbl")
    os.makedirs(out, exist_ok=True)
    arch = ["-march=armv8-a", "-mtune=cortex-a57", "-mtp=soft", "-fPIE"]
    libnx = os.path.join(dkp, "libnx")
    env = dict(os.environ, DEVKITPRO=dkp.replace("\\", "/"))
    cflags = ["-g", "-Wall", "-O2", "-ffunction-sections"] + arch + [
        "-I" + os.path.join(libnx, "include"), "-D__SWITCH__", '-DVERSION="v2.4.5"']
    objs = []
    for f in ("main.c", "trampoline.s"):
        o = os.path.join(out, f + ".o")
        flags = cflags if f.endswith(".c") else ["-g", "-x", "assembler-with-cpp"] + arch
        run([gcc] + flags + ["-c", os.path.join(src, "source", f), "-o", o], env=env)
        objs.append(o)
    elf = os.path.join(out, "hbl.elf")
    specs = os.path.join(libnx, "switch.specs").replace("\\", "/")
    run([gcc, "-specs=" + specs, "-g"] + arch + ["-Wl,-wrap,exit"] + objs +
        ["-L" + os.path.join(libnx, "lib"), "-lnx", "-o", elf], env=env)
    nso = os.path.join(out, "main")
    npdm = os.path.join(out, "main.npdm")
    run([os.path.join(tools, "elf2nso" + exe), elf, nso])
    run([os.path.join(tools, "npdmtool" + exe), os.path.join(HERE, "sonicriders.json"), npdm])
    return nso, npdm


# ------------------------------------------------------------------ the packer (hacBrewPack)
def build_hacbrewpack(work, cc):
    exe = os.path.join(work, "hacbrewpack" + (".exe" if os.name == "nt" else ""))
    if os.path.isfile(exe):
        return exe
    src = fetch(work, "hacBrewPack")
    cfiles = [f for f in sorted(os.listdir(src)) if f.endswith(".c")]
    mbed = os.path.join(src, "mbedtls", "library")
    cfiles += [os.path.join("mbedtls", "library", f) for f in sorted(os.listdir(mbed)) if f.endswith(".c")]
    flags = ["-O2", "-std=gnu11", "-w", "-D_BSD_SOURCE", "-D_POSIX_SOURCE", "-D_POSIX_C_SOURCE=200112L",
             "-D_DEFAULT_SOURCE", "-D__USE_MINGW_ANSI_STDIO=1", "-D_FILE_OFFSET_BITS=64",
             "-I" + os.path.join(src, "mbedtls", "include")]
    libs = ["-lbcrypt", "-lws2_32"] if os.name == "nt" else []
    log("  compiling hacBrewPack")
    run(shlex.split(cc, posix=os.name != "nt") + flags + ["-o", exe] + cfiles + libs, cwd=src)
    return exe


# ------------------------------------------------------------------ title info and icon
def make_nacp(dkp, path):
    exe = ".exe" if os.name == "nt" else ""
    run([os.path.join(dkp, "tools", "bin", "nacptool" + exe), "--create", "Sonic Riders",
         "SEGA (native port by fans)", "1.0.0", path, "--titleid=" + TITLE_ID])


def icon_pixels(src):
    """256 x 256 RGB rows: the picture fills the square (letterboxed in its edge colour if not square)"""
    px = android_icon.load(src)
    h, w = len(px), len(px[0])
    bg = android_icon.edge_colour(px)[:3]
    if len(open(src, "rb").read()) == android_icon.make_icon.REL_SIZE:
        inner = 224  # the small save icon: a margin
    else:
        inner = 256
    s = inner / max(w, h)
    pw, ph = max(1, round(w * s)), max(1, round(h * s))
    if pw == ph:
        pic = android_icon.resample(px, pw)
    else:  # square canvas, then resample
        n = max(w, h)
        sq = [[bg + (255,)] * n for _ in range(n)]
        for y in range(h):
            for x in range(w):
                sq[y + (n - h) // 2][x + (n - w) // 2] = px[y][x]
        pic, pw, ph = android_icon.resample(sq, inner), inner, inner
    out = [[bg] * 256 for _ in range(256)]
    ox, oy = (256 - pw) // 2, (256 - ph) // 2
    for y in range(ph):
        for x in range(pw):
            r, g, b, a = pic[y][x]
            out[oy + y][ox + x] = tuple((c * a + k * (255 - a)) // 255 for c, k in zip((r, g, b), bg))
    return out


# a small baseline JPEG writer (4:4:4, the standard Huffman tables): the HOME menu wants a JPEG icon
_ZZ = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14,
       21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60,
       61, 54, 47, 55, 62, 63]
_QL = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29,
       51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121,
       120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
_QC = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99,
       99, 99, 99, 99] + [99] * 32
_DC_L = ([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], list(range(12)))
_DC_C = ([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], list(range(12)))
_AC_L = ([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D], bytes.fromhex(
    "01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738"
    "393a434445464748494a535455565758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5"
    "a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa"))
_AC_C = ([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], bytes.fromhex(
    "000102031104052131061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a262728292a353637"
    "38393a434445464748494a535455565758595a636465666768696a737475767778797a82838485868788898a92939495969798999aa2a3"
    "a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9fa"))


def _codes(spec):
    counts, syms = spec
    table, code, k = {}, 0, 0
    for n in range(16):
        for _ in range(counts[n]):
            table[syms[k]] = (code, n + 1)
            code += 1
            k += 1
        code <<= 1
    return table


def jpeg(rows, quality=92):
    import math
    h, w = len(rows), len(rows[0])
    sc = 5000 / quality if quality < 50 else 200 - quality * 2
    ql = [min(255, max(1, (q * sc + 50) // 100)) for q in _QL]
    qc = [min(255, max(1, (q * sc + 50) // 100)) for q in _QC]
    cos = [[math.cos((2 * x + 1) * u * math.pi / 16) * (math.sqrt(0.5) if u == 0 else 1) for x in range(8)]
           for u in range(8)]
    dcl, acl, dcc, acc = _codes(_DC_L), _codes(_AC_L), _codes(_DC_C), _codes(_AC_C)
    bits = bytearray()
    acc_bits = [0, 0]

    def put(code, n):
        acc_bits[0] = (acc_bits[0] << n) | code
        acc_bits[1] += n
        while acc_bits[1] >= 8:
            acc_bits[1] -= 8
            byte = (acc_bits[0] >> acc_bits[1]) & 0xFF
            bits.append(byte)
            if byte == 0xFF:
                bits.append(0)

    def enc(v):
        a = abs(v)
        n = a.bit_length()
        return n, (v if v >= 0 else v + (1 << n) - 1)

    def block(px, q, dc_t, ac_t, prev):
        tmp = [[sum(px[y][x] * cos[u][x] for x in range(8)) for u in range(8)] for y in range(8)]
        coef = [0] * 64
        for v in range(8):
            for u in range(8):
                s = sum(tmp[y][u] * cos[v][y] for y in range(8)) / 4
                coef[v * 8 + u] = int(round(s / q[v * 8 + u]))
        zz = [coef[i] for i in _ZZ]
        n, b = enc(zz[0] - prev)
        put(*dc_t[n]); put(b, n) if n else None
        run0 = 0
        for k in range(1, 64):
            if zz[k] == 0:
                run0 += 1
                continue
            while run0 > 15:
                put(*ac_t[0xF0]); run0 -= 16
            n, b = enc(zz[k])
            put(*ac_t[(run0 << 4) | n]); put(b, n)
            run0 = 0
        if run0:
            put(*ac_t[0x00])
        return zz[0]

    prev = [0, 0, 0]
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            Y, Cb, Cr = [], [], []
            for y in range(8):
                ry, rb, rr = [], [], []
                for x in range(8):
                    r, g, b = rows[min(h - 1, by + y)][min(w - 1, bx + x)]
                    ry.append(0.299 * r + 0.587 * g + 0.114 * b - 128)
                    rb.append(-0.168736 * r - 0.331264 * g + 0.5 * b)
                    rr.append(0.5 * r - 0.418688 * g - 0.081312 * b)
                Y.append(ry); Cb.append(rb); Cr.append(rr)
            prev[0] = block(Y, ql, dcl, acl, prev[0])
            prev[1] = block(Cb, qc, dcc, acc, prev[1])
            prev[2] = block(Cr, qc, dcc, acc, prev[2])
    if acc_bits[1]:
        put((1 << (8 - acc_bits[1])) - 1, 8 - acc_bits[1])

    def seg(marker, payload):
        return struct.pack(">BBH", 0xFF, marker, len(payload) + 2) + payload

    def dht(cls, ident, spec):
        return bytes([cls << 4 | ident]) + bytes(spec[0]) + bytes(spec[1])

    out = b"\xFF\xD8" + seg(0xE0, b"JFIF\0\x01\x01\0\0\x01\0\x01\0\0")
    out += seg(0xDB, b"\0" + bytes(ql[i] for i in _ZZ) + b"\x01" + bytes(qc[i] for i in _ZZ))
    out += seg(0xC0, struct.pack(">BHHB", 8, h, w, 3) + bytes([1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1]))
    out += seg(0xC4, dht(0, 0, _DC_L) + dht(1, 0, _AC_L) + dht(0, 1, _DC_C) + dht(1, 1, _AC_C))
    out += seg(0xDA, bytes([3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0]))
    return out + bytes(bits) + b"\xFF\xD9"


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--devkitpro", required=True)
    ap.add_argument("--cc", default="", help="host C compiler command for hacBrewPack (e.g. a clang path)")
    ap.add_argument("--keys", default="", help="your console's prod.keys")
    ap.add_argument("--icon", default="", help="your picture (.png/.ico); default: the game's save icon")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "nsp", "SonicRiders.nsp"))
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "nsp", "work"))
    ap.add_argument("--icon-only", metavar="JPG", help="only write the icon JPEG (to check a picture)")
    ap.add_argument("--print-hashes", action="store_true", help="(maintainers) print the sources' tree hashes")
    a = ap.parse_args()
    try:
        icon_src = a.icon or os.path.join(ROOT, "baserom", "_Main.rel")
        if not os.path.isfile(icon_src):
            raise Error("icon picture not found: %s" % icon_src)
        log("Icon: %s" % (a.icon or "the game's memory card icon from your disc"))
        jpg = jpeg(icon_pixels(icon_src))
        if a.icon_only:
            open(a.icon_only, "wb").write(jpg)
            return 0
        if a.print_hashes:
            for name in SOURCES:
                try:
                    fetch(a.work, name, print_hash=True)
                except Error as e:
                    log("  %s" % e)
            return 0
        if not os.path.isfile(a.keys):
            raise Error("prod.keys not found: %s (dump them from your Switch with Lockpick_RCM)" % a.keys)
        if not a.cc:
            raise Error("--cc (a C compiler for this PC) is needed to build hacBrewPack")
        os.makedirs(a.work, exist_ok=True)
        log("Building the loader (nx-hbloader)")
        nso, npdm = build_hbloader(a.work, a.devkitpro)
        log("Building the packer (hacBrewPack)")
        hbp = build_hacbrewpack(a.work, a.cc)
        log("Packing the NSP with your keys")
        stage = os.path.join(a.work, "pack")
        if os.path.isdir(stage):
            shutil.rmtree(stage)
        for d in ("exefs", "control"):
            os.makedirs(os.path.join(stage, d))
        shutil.copy(nso, os.path.join(stage, "exefs", "main"))
        shutil.copy(npdm, os.path.join(stage, "exefs", "main.npdm"))
        make_nacp(a.devkitpro, os.path.join(stage, "control", "control.nacp"))
        with open(os.path.join(stage, "control", "icon_AmericanEnglish.dat"), "wb") as f:
            f.write(jpg)
        run([hbp, "-k", os.path.abspath(a.keys), "--titleid", TITLE_ID, "--exefsdir", "exefs", "--controldir",
             "control", "--noromfs", "--nologo", "--nspdir", "out", "--ncadir", "nca", "--tempdir", "temp",
             "--backupdir", "backup"], cwd=stage)
        nsps = [f for f in os.listdir(os.path.join(stage, "out")) if f.endswith(".nsp")]
        if not nsps:
            raise Error("hacBrewPack made no NSP (are the keys complete? see build/nsp/work/pack)")
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        shutil.copy(os.path.join(stage, "out", nsps[0]), a.out)
        shutil.rmtree(os.path.join(stage, "backup"), ignore_errors=True)  # it holds a copy of the key area
        log("Done: %s" % a.out)
        return 0
    except (Error, OSError) as e:
        log("ERROR: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
