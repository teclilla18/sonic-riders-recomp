#!/usr/bin/env python3
"""Make config/GXEE8P/hot_functions.txt: the recompiled game functions that run most, hottest first.

The recompiler (tools/recomp/gcrecomp.py) emits these first, in this order, into the first generated
files, so the code that runs during a race sits together in the program instead of being spread over
28 MB (fewer instruction-cache and TLB misses on the Switch's Cortex-A57; SwitchWakerHD round 8).

usage: hot_functions.py [--pc PROFILE.txt MAP]... [--switch LOG]... [--max N] [-o OUT]

  --pc      a desktop profile (SR_PROFILE=1, optionally SR_PROFILE_WINDOW / SR_PROFILE_OUT) and the
            linker map of the build that made it (build/host-mingw/sonicriders.map)
  --switch  a Switch log with SR_PROFILE=1 "[profile]" reports (function and its caller per line)

Each source counts as the share of its own busy samples (idle-loop and runtime entries left out),
so a few long runs do not drown the others. The list holds addresses only: no game code.
"""
import bisect
import re
import sys

IDLE = {0x8000D460, 0x80010428, 0x8000BB6C}  # the OS idle loop and its helpers: hot, but waiting


def pc_profile(prof, mapf):
    syms = []
    for line in open(mapf, errors="replace"):
        m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", line)
        if m:
            syms.append((int(m.group(2), 16) - 0x140000000, m.group(1)))
    if not syms:
        for line in open(mapf, errors="replace"):
            m = re.match(r"([0-9a-fA-F]{8,16}) [0-9a-fA-F]{8,16} +\d+ {9,}(\S+)$", line)
            if m:
                syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    addrs = [a for a, _ in syms]
    counts = {}
    for line in open(prof):
        off, n = line.split()
        off = int(off, 16) & 0x7FFFFFFF
        i = bisect.bisect_right(addrs, off) - 1
        if i < 0:
            continue
        m = re.match(r"f_([0-9A-F]{8})(?:_orig)?$", syms[i][1])
        if m:
            a = int(m.group(1), 16)
            if a not in IDLE:
                counts[a] = counts.get(a, 0) + int(n)
    return counts


def switch_log(path):
    counts = {}
    for line in open(path, errors="replace"):
        m = re.match(r"\[profile\]\s+([\d.]+)%\s+([0-9A-F]{8})\s+from\s+([0-9A-F]{8})", line)
        if not m:
            continue
        share, fn, caller = float(m.group(1)), int(m.group(2), 16), int(m.group(3), 16)
        if fn >= 0xF0000000 or fn in IDLE:
            continue
        counts[fn] = counts.get(fn, 0) + share
        if caller < 0xF0000000 and caller not in IDLE:
            counts[caller] = counts.get(caller, 0) + share / 2  # an address inside the caller
    return counts


def main():
    args = sys.argv[1:]
    out, mx, sources = "config/GXEE8P/hot_functions.txt", 3000, []
    i = 0
    while i < len(args):
        if args[i] == "--pc":
            sources.append(("pc", pc_profile(args[i + 1], args[i + 2])))
            i += 3
        elif args[i] == "--switch":
            sources.append(("switch", switch_log(args[i + 1])))
            i += 2
        elif args[i] == "--max":
            mx = int(args[i + 1])
            i += 2
        elif args[i] == "-o":
            out = args[i + 1]
            i += 2
        else:
            sys.exit(__doc__)
    score = {}
    for kind, counts in sources:
        total = sum(counts.values()) or 1
        for a, n in counts.items():
            score[a] = score.get(a, 0.0) + n / total
    ranked = sorted(score, key=lambda a: -score[a])[:mx]
    with open(out, "w", newline="\n") as f:
        f.write("# Recompiled game functions that run most, hottest first (tools/hot_functions.py): the\n"
                "# recompiler emits them first so hot code sits together. Addresses only. Sources: %s\n"
                % ", ".join("%s (%d functions)" % (k, len(c)) for k, c in sources))
        for a in ranked:
            f.write("%08X\n" % a)
    print("%d functions written to %s" % (len(ranked), out))


if __name__ == "__main__":
    main()
