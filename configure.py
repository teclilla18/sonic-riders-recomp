#!/usr/bin/env python3
"""Generate build.ninja for the port.

usage: python configure.py [--target host|switch]

Pipeline (everything under build/ is derived from the game and never committed):
  1. dtk analysis       baserom/main.dol + _Main.rel -> config/GXEE8P/{,_Main/}symbols.txt
                        (run once by hand: build/tools/dtk.exe dol split config/GXEE8P/config.yml build/GXEE8P)
  2. recompile          tools/recomp/gcrecomp.py -> build/gen/*.c
  3. compile + link     build/gen/*.c + runtime/src/* -> build/<target>/sonicriders(.exe|.nro)

host: --toolchain msvc (default): clang with the MSVC/SDK headers (run `. tools/host/env.ps1` first)
      --toolchain mingw: llvm-mingw (--llvm-mingw DIR) with SDL2's MinGW build (--sdl DIR); the setup
      tool (setup/setup.ps1) uses this one
switch: devkitA64 + libnx + deko3d (devkitPro: $DEVKITPRO, --devkitpro DIR, or C:/devkitPro)
"""
import argparse
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "tools"))
from ninja_syntax import Writer  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--target", choices=["host", "switch"], default="host")
ap.add_argument("--toolchain", choices=["msvc", "mingw"], default="msvc", help="host compiler setup")
ap.add_argument("--llvm-mingw", default="", help="llvm-mingw folder (host, --toolchain mingw)")
ap.add_argument("--sdl", default="", help="SDL2 development folder (host; default: build/ext/SDL2-*)")
ap.add_argument("--devkitpro", default="", help="devkitPro folder (switch)")
ap.add_argument("--no-base-reg", action="store_true", help="switch: keep the guest memory base out of x28 (for comparisons)")
args = ap.parse_args()

ROOT = os.path.dirname(os.path.abspath(__file__))
GEN = "build/gen"
OUT = "build/%s" % ("host-mingw" if args.target == "host" and args.toolchain == "mingw" else args.target)
py = sys.executable.replace("\\", "/")
if " " in py:
    py = '"%s"' % py  # a Python installed under a folder with spaces

gen_c = sorted(glob.glob(os.path.join(ROOT, GEN, "code_*.c")))
if not gen_c:
    # the list of generated files is needed to write build.ninja: recompile once now
    import subprocess
    print("build/gen is empty: recompiling first", flush=True)
    subprocess.check_call([sys.executable, "tools/recomp/gcrecomp.py", "config/GXEE8P", "baserom", GEN], cwd=ROOT)
    gen_c = sorted(glob.glob(os.path.join(ROOT, GEN, "code_*.c")))
gen_c = [os.path.relpath(p, ROOT).replace("\\", "/") for p in gen_c] + [GEN + "/table.c"]
rt_c = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "runtime/src/*.c")))
rt_cpp = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "runtime/src/*.cpp")))
plat = "runtime/src/platform/%s" % args.target
rt_c += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, plat, "*.c")))
rt_cpp += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, plat, "*.cpp")))

def build_version():
    """the VERSION file that tools/make_release.py puts in the release zip, else the git commit"""
    import subprocess
    try:
        with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f:
            v = f.read().strip()
            if v:
                return v
    except OSError:
        pass
    try:
        return subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=8"], cwd=ROOT,
                              capture_output=True, text=True, check=True).stdout.strip() or "unknown"
    except (OSError, subprocess.CalledProcessError):
        return "unknown"

# the build id for the log's first line; rewritten only when it changes (no needless rebuild)
version_c = OUT + "/gen/version.c"
os.makedirs(os.path.join(ROOT, OUT, "gen"), exist_ok=True)
text = 'const char g_build_version[] = "%s";\n' % build_version().replace("\\", "").replace('"', "")
try:
    with open(os.path.join(ROOT, version_c), encoding="utf-8") as f:
        old = f.read()
except OSError:
    old = None
if old != text:
    with open(os.path.join(ROOT, version_c), "w", encoding="utf-8") as f:
        f.write(text)
rt_c.append(version_c)

inc = "-Iruntime/include -Iruntime/src -I%s -I%s/gen" % (GEN, OUT)
# generated code: no FMA contraction (Gekko rounds the product of fmadds; ppc.h models it exactly)
gen_flags = "-O2 -ffp-contract=off -fno-strict-aliasing -fwrapv -w"

def cmd_path(p, program=False):
    """A path for a build command: relative to the repository when it can be (no spaces from the
    folders above it), backslashes for a program, quoted if it still has spaces."""
    # the shorter of the absolute and relative forms that has no spaces: Windows resolves a relative
    # path against the build folder before shortening it, so a deep install folder plus many ".."
    # went over the 260-character limit ("CreateProcess: The filename or extension is too long")
    a = os.path.abspath(p)
    try:
        r = os.path.relpath(p, ROOT)
    except ValueError:  # another drive
        r = a
    spaceless = [x for x in (a, r) if " " not in x]
    p = min(spaceless, key=len) if spaceless else r
    p = os.path.normpath(p) if program else p.replace("\\", "/")
    return '"%s"' % p if " " in p else p


with open(os.path.join(ROOT, "build.ninja"), "w") as f:
    n = Writer(f)
    n.variable("ninja_required_version", "1.10")
    if args.target == "host":
        # SDL2 (window, GL context, input, audio): downloaded, not committed
        def pick_sdl(pred):
            for d in sorted(glob.glob(os.path.join(ROOT, "build/ext/SDL2-*")), reverse=True):
                if pred(d):
                    return d
            return ""
        if args.toolchain == "mingw":
            lm = args.llvm_mingw.replace("\\", "/")
            if not os.path.isfile(os.path.join(lm, "bin", "x86_64-w64-mingw32-clang.exe")):
                sys.exit("llvm-mingw not found (--llvm-mingw DIR)")
            sdl_root = args.sdl or pick_sdl(lambda d: glob.glob(os.path.join(d, "*", "x86_64-w64-mingw32")))
            sdl_m = glob.glob(os.path.join(sdl_root, "*", "x86_64-w64-mingw32")) + glob.glob(os.path.join(sdl_root, "x86_64-w64-mingw32"))
            if not sdl_m:
                sys.exit("SDL2 MinGW development files not found (--sdl DIR: SDL2-devel-2.32.10-mingw.zip extracted)")
            sdl = cmd_path(sdl_m[0])
            # the program path in backslashes: Windows' process creation does not resolve a relative
            # path with forward slashes
            n.variable("cc", cmd_path(os.path.join(lm, "bin", "x86_64-w64-mingw32-clang.exe"), program=True))
            n.variable("cxx", cmd_path(os.path.join(lm, "bin", "x86_64-w64-mingw32-clang++.exe"), program=True))
            hostinc = "%s -Iruntime/include/compat -I%s/include/SDL2 -DSDL_MAIN_HANDLED -D_WIN32_WINNT=0x0A00" % (inc, sdl)
            n.variable("cflags", "-std=gnu11 -g " + hostinc)
            n.variable("cxxflags", "-std=c++20 -g " + hostinc)
            # a windowed program (no console window); the log goes to sonicriders.log then (platform_host.c)
            n.variable("ldflags", "-g -L%s/lib -lSDL2 -lopengl32 -lwinmm -Wl,--subsystem,windows -Wl,-Map=%s/sonicriders.map" % (sdl, OUT))
            sdl_dll = sdl + "/bin/SDL2.dll"
        else:
            sdl = args.sdl or pick_sdl(lambda d: os.path.isdir(os.path.join(d, "lib", "x64")))
            if not sdl:
                sys.exit("SDL2 not found: extract SDL2-devel-2.32.10-VC.zip into build/ext (see NOTES.md)")
            sdl = os.path.relpath(sdl, ROOT).replace("\\", "/")
            n.variable("cc", "clang")
            n.variable("cxx", "clang++")
            n.variable("cflags", "-std=gnu11 -g %s -I%s/include -DSDL_MAIN_HANDLED -D_CRT_SECURE_NO_WARNINGS" % (inc, sdl))
            n.variable("cxxflags", "-std=c++20 -g %s -I%s/include -DSDL_MAIN_HANDLED -D_CRT_SECURE_NO_WARNINGS" % (inc, sdl))
            n.variable("ldflags", "-g -fuse-ld=lld -L%s/lib/x64 -lSDL2 -lopengl32 -lwinmm -Wl,/MAP:%s/sonicriders.map" % (sdl, OUT))
            sdl_dll = sdl + "/lib/x64/SDL2.dll"
        exe = OUT + "/sonicriders.exe"
    else:
        dkp = (args.devkitpro or os.environ.get("DEVKITPRO", "")).replace("\\", "/")
        if not os.path.isdir(os.path.join(dkp, "devkitA64")):  # msys-style /opt/devkitpro from a Windows shell
            dkp = "C:/devkitPro"
        if not os.path.isdir(os.path.join(dkp, "devkitA64")):
            sys.exit("devkitPro not found (--devkitpro DIR, $DEVKITPRO or C:/devkitPro)")
        if " " in dkp:
            sys.exit("the devkitPro folder must not contain spaces (libnx's switch.specs reads $DEVKITPRO): %s" % dkp)
        arch = "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
        if not args.no_base_reg:
            # the guest memory base stays in x28 (runtime/include/ppc.h): nothing we compile may use it
            arch += " -ffixed-x28 -DPPC_FIXED_X28"
            gen_flags += " -DPPC_GEN_CODE -DPPC_BASE_X28"
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
           description="RECOMP", restat=True, pool="console",
           generator=True)  # no ninja log yet (first build) must not mean "recompile again": mtimes decide

    recomp_inputs = ["tools/recomp/gcrecomp.py", "tools/recomp/ppc2c.py", "tools/recomp/crlive.py",
                     "tools/recomp/leaflocal.py", "tools/recomp/singleflow.py", "tools/gcbin.py", "config/GXEE8P/symbols.txt",
                     "config/GXEE8P/_Main/symbols.txt"] + [p for p in ("config/GXEE8P/hle.txt", "config/GXEE8P/patches.txt", "config/GXEE8P/hot_functions.txt")
                                                           if os.path.exists(os.path.join(ROOT, p))]
    n.build(gen_c + [GEN + "/funcs.h"], "recomp", recomp_inputs)

    # shaders: GL strings (host) or uam-compiled deko3d binaries (Switch)
    shader_src = ["runtime/shaders/gx_common.glsl", "runtime/shaders/gx.vert", "runtime/shaders/gx.frag",
                  "runtime/shaders/overlay.vert", "runtime/shaders/overlay.frag", "tools/embed_shaders.py"]
    if args.target == "host":
        shader_h = OUT + "/gen/gx_shaders_gl.h"
        n.rule("shaders", "%s tools/embed_shaders.py gl $out" % py, description="SHADERS $out")
    else:
        shader_h = OUT + "/gen/gx_shaders_dksh.h"
        n.rule("shaders", "%s tools/embed_shaders.py dksh $out %s/tools/bin/uam.exe %s/shaders" % (py, dkp, OUT),
               description="UAM $out")
    n.build(shader_h, "shaders", shader_src)
    if args.target == "switch":
        # specialized fragment shaders from the recorded keys (game-derived: build/shaderkeys/, not in git)
        key_files = sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "build/shaderkeys/*.txt")))
        spec_h = OUT + "/gen/gx_spec_dksh.h"
        n.rule("specshaders", "%s tools/gen_shaders.py $out %s/tools/bin/uam.exe %s/spec $in_keys" % (py, dkp, OUT),
               description="SPEC $out")
        n.build(spec_h, "specshaders", ["tools/gen_shaders.py", "tools/embed_shaders.py", "runtime/shaders/gx.frag",
                                        "runtime/shaders/gx_common.glsl"] + key_files,
                variables={"in_keys": " ".join(key_files)})
        shader_h = [shader_h, spec_h]
    else:
        shader_h = [shader_h]

    objs = []
    for src in gen_c:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cc", src, variables={"extra": gen_flags})
        objs.append(o)
    for src in rt_c:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cc", src, implicit=[GEN + "/funcs.h"] + shader_h, variables={"extra": "-O2 -Wall"})
        objs.append(o)
    for src in rt_cpp:
        o = "%s/obj/%s.o" % (OUT, src.replace("/", "_"))
        n.build(o, "cxx", src, implicit=[GEN + "/funcs.h"] + shader_h, variables={"extra": "-O2 -Wall"})
        objs.append(o)
    # optional program icon (PC, llvm-mingw): build/icon.ico if present. It lives in build/ (never
    # committed); the setup or the user puts it there.
    icon = "build/icon.ico"
    if args.target == "host" and args.toolchain == "mingw" and os.path.isfile(os.path.join(ROOT, icon)):
        rc = OUT + "/gen/icon.rc"
        with open(os.path.join(ROOT, rc), "w", encoding="utf-8") as f:
            f.write('1 ICON "%s"\n' % os.path.abspath(os.path.join(ROOT, icon)).replace("\\", "/"))
        n.rule("rc", "%s -O coff -i $in -o $out" % cmd_path(os.path.join(lm, "bin", "x86_64-w64-mingw32-windres.exe"), program=True),
               description="RC $out")
        n.build(OUT + "/obj/icon.res.o", "rc", rc, implicit=[icon])
        objs.append(OUT + "/obj/icon.res.o")
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
        n.build(OUT + "/SDL2.dll", "copy", sdl_dll)
        n.default(OUT + "/SDL2.dll")
    n.build("gen_objs", "phony", objs[:len(gen_c)])
    n.default(exe)
print("wrote build.ninja (%s): %d generated + %d runtime sources" % (args.target, len(gen_c), len(rt_c) + len(rt_cpp)))
