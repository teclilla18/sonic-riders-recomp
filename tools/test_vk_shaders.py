#!/usr/bin/env python3
"""Validate generated Vulkan shaders and audit optional capabilities/secondary blend outputs.

Run after the Android shader build. Optionally check Switch scratch with --dksh build/switch.
Only reads locally generated runtime shaders; does not read game binaries or generated PPC code.
"""
import argparse
from pathlib import Path
import struct
import subprocess


def inspect(path):
    words = struct.unpack("<%dI" % (path.stat().st_size // 4), path.read_bytes())
    assert words[0] == 0x07230203 and words[1] <= 0x00010300, path
    caps, indexes = set(), set()
    i = 5
    while i < len(words):
        size, op = words[i] >> 16, words[i] & 65535
        assert size and i + size <= len(words), path
        if op == 17:  # OpCapability: Shader and ImageQuery need no optional device feature
            caps.add(words[i + 1])
        if op == 71 and size == 4 and words[i + 2] == 32:  # OpDecorate Index
            indexes.add(words[i + 3])
        i += size
    assert caps <= {1, 50}, (path, caps)
    return indexes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work", default="build/android-arm64-v8a")
    ap.add_argument("--validator", required=True)
    ap.add_argument("--dksh")
    a = ap.parse_args()
    root = Path(a.work)
    paths = sorted(root.glob("shaders/*.spv")) + sorted(root.glob("spec/*.spv"))
    assert paths, "build the Vulkan shaders first"
    single = 0
    for p in paths:
        subprocess.run([a.validator, "--target-env", "vulkan1.1", str(p)], check=True, capture_output=True)
        indexes = inspect(p)
        glsl = p.with_suffix(".frag.glsl") if p.parent.name == "spec" else p.with_suffix(".glsl")
        source = glsl.read_text()
        if "#define GX_SINGLE_SOURCE 1" in source:
            assert 1 not in indexes, p
            single += 1
        elif p.name.startswith("gx_frag") or p.parent.name == "spec":
            assert 1 in indexes, p
    assert single >= 2, "single-source ubershaders must be generated"
    print("PASS: %d SPIR-V modules validate for Vulkan 1.1; only Shader/ImageQuery capabilities; %d single-source variants" %
          (len(paths), single))
    if a.dksh:
        dksh = list(Path(a.dksh).rglob("*.dksh"))
        assert dksh, "build the Switch shaders first"
        for p in dksh:
            data = p.read_bytes()
            assert data[:4] == b"DKSH", p
            offset = struct.unpack_from("<I", data, 16)[0]
            assert struct.unpack_from("<I", data, offset + 20)[0] == 0, p
        print("PASS: all %d Switch shaders have per_warp_scratch 0" % len(dksh))


if __name__ == "__main__":
    main()
