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
    # the list of generated files is needed to write build.ninja: recompile once now
    import subprocess
    print("build/gen is empty: recompiling first")
    subprocess.check_call([sys.executable, "tools/recomp/gcrecomp.py", "config/GXEE8P", "baserom", GEN], cwd=ROOT)
    gen_c = sorted(glob.glob(os.path.join(ROOT, GEN, "code_*.c")))
gen_c = [os.path.relpath(p, ROOT).replace("\\", "/") for p in gen_c] + [GEN + "/table.c"]
rt_c = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "runtime/src/*.c")))
rt_cpp = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "runtime/src/*.cpp")))
plat = "runtime/src/platform/%s" % args.target
rt_c += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, plat, "*.c")))
rt_cpp += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, plat, "*.cpp")))

inc = "-Iruntime/include -Iruntime/src -I%s -I%s/gen" % (GEN, OUT)
# generated code: no FMA contraction (Gekko rounds the product of fmadds; ppc.h models it exactly)
gen_flags = "-O2 -ffp-contract=off -fno-strict-aliasing -fwrapv -w"

with open(os.path.join(ROOT, "build.ninja"), "w") as f:
    n = Writer(f)
    n.variable("ninja_required_version", "1.10")
    if args.target == "host":
        # SDL2 (window, GL context, input, audio) from build/ext (downloaded, not committed)
        sdl = glob.glob(os.path.join(ROOT, "build/ext/SDL2-*"))
        if not sdl:
            sys.exit("SDL2 not found: extract SDL2-devel-2.32.10-VC.zip into build/ext (see NOTES.md)")
        sdl = os.path.relpath(sorted(sdl)[-1], ROOT).replace("\\", "/")
        n.variable("cc", "clang")
        n.variable("cxx", "clang++")
        n.variable("cflags", "-std=gnu11 -g %s -I%s/include -DSDL_MAIN_HANDLED -D_CRT_SECURE_NO_WARNINGS" % (inc, sdl))
        n.variable("cxxflags", "-std=c++20 -g %s -I%s/include -DSDL_MAIN_HANDLED -D_CRT_SECURE_NO_WARNINGS" % (inc, sdl))
        n.variable("ldflags", "-g -fuse-ld=lld -L%s/lib/x64 -lSDL2 -lopengl32" % sdl)
        exe = OUT + "/sonicriders.exe"
    else:
        dkp = os.environ.get("DEVKITPRO", "").replace("\\", "/")
        if not os.path.isdir(os.path.join(dkp, "devkitA64")):  # msys-style /opt/devkitpro from a Windows shell
            dkp = "C:/devkitPro"
        arch = "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
        n.variable("cc", dkp + "/devkitA64/bin/aarch64-none-elf-gcc")
        n.variable("cxx", dkp + "/devkitA64/bin/aarch64-none-elf-g++")
        n.variable("cflags", "-std=gnu11 -g %s %s -D__SWITCH__ -I%s/libnx/include -ffunction-sections" % (arch, inc, dkp))
        n.variable("cxxflags", "-std=gnu++20 -g %s %s -D__SWITCH__ -I%s/libnx/include -ffunction-sections -fno-rtti -fno-exceptions" % (arch, inc, dkp))
        n.variable("ldflags", "-specs=%s/libnx/switch.specs -g %s -L%s/libnx/lib -Wl,-Map,%s/sonicriders.map -ldeko3d -lnx -lm"
                   % (dkp, arch, dkp, OUT))
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

    # shaders: GL strings (host) or uam-compiled deko3d binaries (Switch)
    shader_src = ["runtime/shaders/gx_common.glsl", "runtime/shaders/gx.vert", "runtime/shaders/gx.frag", "tools/embed_shaders.py"]
    if args.target == "host":
        shader_h = OUT + "/gen/gx_shaders_gl.h"
        n.rule("shaders", "%s tools/embed_shaders.py gl $out" % py, description="SHADERS $out")
    else:
        shader_h = OUT + "/gen/gx_shaders_dksh.h"
        n.rule("shaders", "%s tools/embed_shaders.py dksh $out %s/tools/bin/uam.exe %s/shaders" % (py, dkp, OUT),
               description="UAM $out")
    n.build(shader_h, "shaders", shader_src)

    objs = []
    for src in gen_c:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cc", src, variables={"extra": gen_flags})
        objs.append(o)
    for src in rt_c:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cc", src, implicit=[GEN + "/funcs.h", shader_h], variables={"extra": "-O2 -Wall"})
        objs.append(o)
    for src in rt_cpp:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cxx", src, implicit=[GEN + "/funcs.h", shader_h], variables={"extra": "-O2 -Wall"})
        objs.append(o)
    n.build(exe, "link", objs)
    if args.target == "switch":
        n.rule("nacp", "%s/tools/bin/nacptool.exe --create \"Sonic Riders\" \"native port (own disc image required)\" 0.1.0 $out" % dkp,
               description="NACP $out")
        n.build(OUT + "/sonicriders.nacp", "nacp")
        n.rule("nro", "%s/tools/bin/elf2nro.exe $in $out --nacp=%s/sonicriders.nacp" % (dkp, OUT), description="NRO $out")
        n.build(OUT + "/sonicriders.nro", "nro", exe, implicit=[OUT + "/sonicriders.nacp"])
        n.default(OUT + "/sonicriders.nro")
    if args.target == "host":
        n.rule("copy", "%s -c \"import shutil,sys; shutil.copy(sys.argv[1], sys.argv[2])\" $in $out" % py, description="COPY $out")
        n.build(OUT + "/SDL2.dll", "copy", sdl + "/lib/x64/SDL2.dll")
        n.default(OUT + "/SDL2.dll")
    n.build("gen_objs", "phony", objs[:len(gen_c)])
    n.default(exe)
print("wrote build.ninja (%s): %d generated + %d runtime sources" % (args.target, len(gen_c), len(rt_c) + len(rt_cpp)))
