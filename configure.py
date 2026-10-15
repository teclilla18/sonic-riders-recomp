#!/usr/bin/env python3
"""Generate build.ninja for the port.

usage: python configure.py [--target host|switch]

Pipeline (everything under build/ is derived from the game and never committed):
  1. dtk analysis       baserom/main.dol + _Main.rel -> config/GXEE8P/{,_Main/}symbols.txt
                        (run once by hand: build/tools/dtk.exe dol split config/GXEE8P/config.yml build/GXEE8P)
  2. recompile          tools/recomp/gcrecomp.py -> build/gen/*.c
  3. compile + link     build/gen/*.c + runtime/src/* -> build/<target>/sonicriders(.exe|.nro)

host: clang (Windows: run `. tools/host/env.ps1` first so clang finds the MSVC/SDK headers)
switch: devkitA64 + libnx + deko3d (devkitPro)
"""
import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "tools"))
from ninja_syntax import Writer  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--target", choices=["host", "switch"], default="host")
args = ap.parse_args()

ROOT = os.path.dirname(os.path.abspath(__file__))
GEN = "build/gen"
OUT = "build/%s" % args.target
py = sys.executable.replace("\\", "/")

gen_c = sorted(glob.glob(os.path.join(ROOT, GEN, "code_*.c")))
if not gen_c:
    sys.exit("build/gen is empty: run `%s tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen` first" % py)
gen_c = [os.path.relpath(p, ROOT).replace("\\", "/") for p in gen_c] + [GEN + "/table.c"]
rt_c = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "runtime/src/*.c")))
rt_cpp = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "runtime/src/*.cpp")))
plat = "runtime/src/platform/%s" % args.target
rt_c += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, plat, "*.c")))
rt_cpp += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, plat, "*.cpp")))

inc = "-Iruntime/include -Iruntime/src -I%s" % GEN
# generated code: no FMA contraction (Gekko rounds the product of fmadds; ppc.h models it exactly)
gen_flags = "-O2 -ffp-contract=off -fno-strict-aliasing -w"

with open(os.path.join(ROOT, "build.ninja"), "w") as f:
    n = Writer(f)
    n.variable("ninja_required_version", "1.10")
    if args.target == "host":
        n.variable("cc", "clang")
        n.variable("cxx", "clang++")
        n.variable("cflags", "-std=gnu11 -g %s -D_CRT_SECURE_NO_WARNINGS" % inc)
        n.variable("cxxflags", "-std=c++20 -g %s -D_CRT_SECURE_NO_WARNINGS" % inc)
        n.variable("ldflags", "-g -fuse-ld=lld")
        exe = OUT + "/sonicriders.exe"
    else:
        dkp = os.environ.get("DEVKITPRO", "C:/devkitPro").replace("\\", "/")
        arch = "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
        n.variable("cc", dkp + "/devkitA64/bin/aarch64-none-elf-gcc")
        n.variable("cxx", dkp + "/devkitA64/bin/aarch64-none-elf-g++")
        n.variable("cflags", "-std=gnu11 -g %s %s -D__SWITCH__ -I%s/libnx/include -ffunction-sections" % (arch, inc, dkp))
        n.variable("cxxflags", "-std=gnu++20 -g %s %s -D__SWITCH__ -I%s/libnx/include -ffunction-sections -fno-rtti -fno-exceptions" % (arch, inc, dkp))
        n.variable("ldflags", "-specs=%s/libnx/switch.specs -g %s -L%s/libnx/lib -ldeko3d -lnx" % (dkp, arch, dkp))
        exe = OUT + "/sonicriders.elf"

    n.rule("cc", "$cc -MMD -MF $out.d $cflags $extra -c $in -o $out", depfile="$out.d", deps="gcc",
           description="CC $in")
    n.rule("cxx", "$cxx -MMD -MF $out.d $cxxflags $extra -c $in -o $out", depfile="$out.d", deps="gcc",
           description="CXX $in")
    n.rule("link", "$cxx $in -o $out $ldflags", description="LINK $out")
    n.rule("recomp", "%s tools/recomp/gcrecomp.py config/GXEE8P baserom %s" % (py, GEN),
           description="RECOMP", restat=True, pool="console")

    recomp_inputs = ["tools/recomp/gcrecomp.py", "tools/recomp/ppc2c.py", "tools/recomp/crlive.py",
                     "tools/recomp/leaflocal.py", "tools/gcbin.py", "config/GXEE8P/symbols.txt",
                     "config/GXEE8P/_Main/symbols.txt"] + (["config/GXEE8P/hle.txt"] if os.path.exists(
                         os.path.join(ROOT, "config/GXEE8P/hle.txt")) else [])
    n.build(gen_c + [GEN + "/funcs.h"], "recomp", recomp_inputs)

    objs = []
    for src in gen_c:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cc", src, variables={"extra": gen_flags})
        objs.append(o)
    for src in rt_c:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cc", src, implicit=[GEN + "/funcs.h"], variables={"extra": "-O2 -Wall"})
        objs.append(o)
    for src in rt_cpp:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cxx", src, implicit=[GEN + "/funcs.h"], variables={"extra": "-O2 -Wall"})
        objs.append(o)
    n.build(exe, "link", objs)
    n.build("gen_objs", "phony", objs[:len(gen_c)])
    n.default(exe)
print("wrote build.ninja (%s): %d generated + %d runtime sources" % (args.target, len(gen_c), len(rt_c) + len(rt_cpp)))
