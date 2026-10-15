#!/usr/bin/env python3
"""Print the disassembly of functions from dtk's asm output (build/GXEE8P/asm).

usage: showfn.py ADDR|NAME [...] [--max N]   (prints at most N lines per function, default 60)
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = [a for a in sys.argv[1:] if not a.startswith("--")]
mx = 60
if "--max" in sys.argv:
    mx = int(sys.argv[sys.argv.index("--max") + 1])
    args = [a for a in args if a != str(mx)]
files = glob.glob(os.path.join(ROOT, "build/GXEE8P/asm/*.s")) + glob.glob(os.path.join(ROOT, "build/GXEE8P/_Main/asm/*.s"))
text = {f: open(f).read() for f in files}
for want in args:
    pat = ("fn_%s" % want.upper()) if re.fullmatch(r"[0-9A-Fa-f]{8}", want) else re.escape(want)
    for f, t in text.items():
        m = re.search(r"^\.fn %s,.*?^\.endfn %s$" % (pat, pat), t, re.S | re.M)
        if m:
            lines = m.group(0).splitlines()
            out = [re.sub(r"^/\* ([0-9A-F]{8}) [0-9A-F]{8}  [0-9A-F ]{11} \*/\t", r"\1  ", l) for l in lines]
            print("\n".join(out[:mx]) + ("\n  ... (%d more lines)" % (len(out) - mx) if len(out) > mx else ""))
            print()
            break
    else:
        print("not found:", want)
