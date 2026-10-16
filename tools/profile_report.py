#!/usr/bin/env python3
"""Map the desktop sampling profiler's output (build/profile.txt, SR_PROFILE=1) to functions.

usage: profile_report.py [profile.txt] [linker map] [--top N]

The map is lld-link's /MAP output (build/host/sonicriders.map). Recompiled game functions are named
f_<guest address>; everything else is the runtime. Prints the hottest functions, and the share of
time in recompiled code versus runtime helpers.
"""
import bisect
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 40
if "--top" in sys.argv:
    args = [a for a in args if a != str(top)]
prof = args[0] if args else "build/profile.txt"
mapf = args[1] if len(args) > 1 else "build/host/sonicriders.map"

syms = []
for line in open(mapf, errors="replace"):  # MSVC-style map: " 0001:00000cd0  f_80003154  0000000140001cd0  obj"
    m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", line)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
image_base = 0x140000000  # "Preferred load address"; profile offsets are relative to the actual base
addrs = [a for a, _ in syms]

counts = {}
total = 0
for line in open(prof):
    off, n = line.split()
    va = image_base + int(off, 16)
    n = int(n)
    i = bisect.bisect_right(addrs, va) - 1
    name = syms[i][1] if i >= 0 else "?"
    counts[name] = counts.get(name, 0) + n
    total += n

game = sum(n for k, n in counts.items() if re.match(r"f_[0-9A-F]{8}", k))
print("%d samples: recompiled game code %.1f%%, runtime %.1f%%" % (total, 100.0 * game / total, 100.0 * (total - game) / total))
for name, n in sorted(counts.items(), key=lambda x: -x[1])[:top]:
    print("%6.2f%%  %s" % (100.0 * n / total, name))
