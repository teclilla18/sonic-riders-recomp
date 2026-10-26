#!/usr/bin/env python3
"""Specialized fragment shaders for deko3d, built ahead of time from recorded keys.

  gen_shaders.py OUT.h UAM WORKDIR [KEYFILE...]

Each key (runtime/src/gx_shader_key.c: the BP registers that decide the ubershader's control flow)
becomes the ubershader source with SPECIALIZED defined: CBP(i) returns the key's constants and the
TEV stages are called one by one, so the compiler folds the branches and the stage loop away. Each is
compiled with uam (cached in WORKDIR by source hash) and embedded in OUT.h as a table sorted by key
hash; render_deko.c binds the matching shader per state and falls back to the ubershader otherwise.

A shader that would need per-warp scratch memory is left out (see NOTES.md, session 4).
Key files are game-derived state: keep them out of git (build/shaderkeys/).
"""
import concurrent.futures
import hashlib
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import embed_shaders  # noqa: E402  (PRELUDE_DK, the shader sources)

SHADERS = embed_shaders.SHADERS


def parse_keys(paths):
    keys = {}
    for p in paths:
        if not os.path.exists(p):
            continue
        for line in open(p):
            f = line.split()
            if len(f) < 3:
                continue
            try:
                h = int(f[0], 16)
                regs = [(int(a, 16), int(b, 16)) for a, b in (x.split(":") for x in f[2:2 + int(f[1])])]
            except ValueError:
                continue
            regs = normalize(regs)
            keys[key_hash(regs)] = regs
    return keys


def normalize(regs):
    """the masks of runtime/src/gx_shader_key.c, so key files recorded by older builds still match"""
    d = dict(regs)
    gen = d.get(0x00, 0)
    stages = ((gen >> 10) & 15) + 1
    nind = (gen >> 16) & 7
    kc = ka = 0
    for s in range(stages):
        ce, ae = d.get(0xC0 + 2 * s, 0), d.get(0xC1 + 2 * s, 0)
        if any((ce >> f) & 15 == 14 for f in (0, 4, 8, 12)):
            kc |= 1 << s
        if any((ae >> f) & 7 == 6 for f in (4, 7, 10, 13)):
            ka |= 1 << s
    out = []
    for r, v in regs:
        if 0xF6 <= r <= 0xFD:
            i = r - 0xF6
            m = 0xF
            if kc >> (2 * i) & 1: m |= 0x1F0
            if ka >> (2 * i) & 1: m |= 0x3E00
            if kc >> (2 * i + 1) & 1: m |= 0x7C000
            if ka >> (2 * i + 1) & 1: m |= 0xF80000
            v &= m
        elif r == 0x25:
            v &= 0xFFFF if nind > 1 else 0xFF
        elif r == 0x26:
            v &= 0xFFFF if nind > 3 else 0xFF if nind > 2 else 0
        elif r == 0x27:
            v &= (1 << (6 * min(nind, 4))) - 1
        out.append((r, v))
    return out


def key_hash(regs):
    """FNV-1a over (register, value) pairs, as gx_frag_key computes it"""
    h = 0xCBF29CE484222325
    for r, v in regs:
        h = ((h ^ r) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ v) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def specialized_source(regs):
    d = dict(regs)
    gen = d.get(0x00, 0)
    stages = ((gen >> 10) & 15) + 1
    nind = (gen >> 16) & 7
    cbp = "uint CBP(int i) {\n  return " + "".join("i == 0x%02X ? 0x%06Xu : " % (r, v) for r, v in regs) + "0u;\n}\n"
    spec = "#define SPEC_STAGES " + " ".join("TEV_STAGE(%d);" % s for s in range(stages)) + "\n"
    common = open(os.path.join(SHADERS, "gx_common.glsl")).read() + "\n"
    body = open(os.path.join(SHADERS, "gx.frag")).read()
    defines = "#define GX_INDIRECT %d\n#define SPECIALIZED 1\n" % (1 if nind else 0)
    return embed_shaders.PRELUDE_DK + defines + common + cbp + spec + body


def scratch(dksh):
    """per-warp scratch bytes of the (single) program in a dksh file"""
    if len(dksh) < 24 or dksh[:4] != b"DKSH":
        return -1
    programs_off = struct.unpack_from("<I", dksh, 16)[0]
    return struct.unpack_from("<I", dksh, programs_off + 20)[0]


def compile_one(uam, work, src):
    tag = hashlib.sha1(src.encode()).hexdigest()[:16]
    glsl = os.path.join(work, "spec_%s.glsl" % tag)
    dksh = os.path.join(work, "spec_%s.dksh" % tag)
    if not os.path.exists(dksh):
        open(glsl, "w").write(src)
        r = subprocess.run([uam, "-s", "frag", "-o", dksh, glsl], capture_output=True, text=True)
        if r.returncode:
            return None, "uam failed for %s:\n%s%s" % (glsl, r.stdout, r.stderr)
    return open(dksh, "rb").read(), None


def main():
    out, uam, work = sys.argv[1], sys.argv[2], sys.argv[3]
    keys = parse_keys(sys.argv[4:])
    os.makedirs(work, exist_ok=True)
    items = sorted(keys.items())
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        futs = [ex.submit(compile_one, uam, work, specialized_source(regs)) for _, regs in items]
        for (h, _), fut in zip(items, futs):
            data, err = fut.result()
            if err:
                print(err, file=sys.stderr)
                continue
            if scratch(data) != 0:
                print("gen_shaders: key %016x needs scratch memory (%d): left to the ubershader" % (h, scratch(data)))
                continue
            results.append((h, data))
    blob = bytearray()
    table = []
    for h, data in results:
        off = len(blob)
        blob += data
        blob += b"\0" * ((-len(blob)) % 256)  # DK_SHADER_CODE_ALIGNMENT
        table.append((h, off, len(data)))
    text = ["/* generated by tools/gen_shaders.py: %d specialized fragment shaders (deko3d) */" % len(table),
            "#pragma once", "#include <stdint.h>", "#define GX_SPEC_COUNT %d" % len(table)]
    n = max(len(table), 1)
    text.append("static const uint64_t k_spec_keys[%d] = {%s};" % (n, ", ".join("0x%016xull" % h for h, _, _ in table) or "0"))
    text.append("static const uint32_t k_spec_off[%d] = {%s};" % (n, ", ".join(str(o) for _, o, _ in table) or "0"))
    text.append("static const uint32_t k_spec_size[%d] = {%s};" % (n, ", ".join(str(s) for _, _, s in table) or "0"))
    text.append("static const uint8_t k_spec_blob[%d] __attribute__((aligned(256))) = {" % max(len(blob), 1))
    for i in range(0, len(blob), 32):
        text.append("    " + ", ".join("0x%02X" % b for b in blob[i:i + 32]) + ",")
    if not blob:
        text.append("    0")
    text.append("};")
    embed_shaders.write_if_changed(out, "\n".join(text) + "\n")
    print("gen_shaders: %d keys, %d shaders (%d KB)" % (len(keys), len(table), len(blob) // 1024))


if __name__ == "__main__":
    main()
