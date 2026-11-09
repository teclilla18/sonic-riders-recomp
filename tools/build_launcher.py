"""Builds the Windows launcher (launcher/launcher.cpp) into build/launcher/SonicRidersLauncher.exe.

    python tools/build_launcher.py [--llvm-mingw <dir>] [--imgui <dir>] [--root-copy]

The launcher holds no game code or data: Win32 + Direct3D 11 + Dear ImGui, linked statically (only Windows'
own DLLs at run time), so a ready-made copy can ship in the release zip. --root-copy also puts it in the
repository root, where it expects to live (next to setup\\, build\\, baserom\\, saves\\).
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(os.environ.get("LOCALAPPDATA", ""), "SonicRidersSetup", "tools")


MANIFEST = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application>
  </compatibility>
</assembly>
"""


def write_png_ico(path, pngs):
    """an .ico whose entries are PNG files (Windows Vista and later); sizes read from each PNG's header"""
    import struct
    datas = [open(p, "rb").read() for p in pngs]
    out = struct.pack("<HHH", 0, 1, len(datas))
    off = 6 + 16 * len(datas)
    for d in datas:
        w, h = struct.unpack(">II", d[16:24])
        out += struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0, 1, 32, len(d), off)
        off += len(d)
    with open(path, "wb") as f:
        f.write(out + b"".join(datas))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--llvm-mingw", default=os.path.join(TOOLS, "llvm-mingw"))
    ap.add_argument("--imgui", default=os.path.join(TOOLS, "imgui"))
    ap.add_argument("--root-copy", action="store_true")
    a = ap.parse_args()

    cxx = os.path.join(a.llvm_mingw, "bin", "x86_64-w64-mingw32-clang++.exe")
    if not os.path.isfile(cxx):
        sys.exit("build_launcher: no compiler at %s (--llvm-mingw)" % cxx)
    im = a.imgui
    srcs = [os.path.join(ROOT, "launcher", "launcher.cpp")]
    srcs += [os.path.join(im, f) for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
    srcs += [os.path.join(im, "backends", f) for f in ("imgui_impl_win32.cpp", "imgui_impl_dx11.cpp")]
    out_dir = os.path.join(ROOT, "build", "launcher")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "SonicRidersLauncher.exe")
    # the program icon as resource APPICON: launcher/icon_256.png + icon_32.png (Super Sonic art the user made with
    # ChatGPT, 2026-11-09; the small one a tighter face crop) as PNG entries of an .ico, else the gold ring
    # tools/launcher_icon.py draws
    ico = os.path.join(out_dir, "launcher.ico")
    pngs = [os.path.join(ROOT, "launcher", "icon_%d.png" % n) for n in (256, 32)]
    if all(os.path.isfile(p) for p in pngs):
        write_png_ico(ico, pngs)
    else:
        ico = os.path.join(out_dir, "launcher.ico")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "launcher_icon.py"), ico], check=True)
    rc, res = os.path.join(out_dir, "launcher.rc"), os.path.join(out_dir, "launcher_res.o")
    with open(rc, "w") as f:
        f.write('APPICON ICON "%s"\n' % ico.replace("\\", "/"))
        # run as the user who starts it: without a manifest Windows treats an exe named "Install ..." (the Switch /
        # Android installer) as a setup program and asks for administrator rights
        man = os.path.join(out_dir, "launcher.manifest")
        with open(man, "w") as m:
            m.write(MANIFEST)
        f.write('1 24 "%s"\n' % man.replace("\\", "/"))  # 24 = RT_MANIFEST
    windres = os.path.join(a.llvm_mingw, "bin", "x86_64-w64-mingw32-windres.exe")
    subprocess.run([windres, rc, "-O", "coff", "-o", res], check=True)
    srcs.append(res)
    cmd = [cxx, "-std=c++17", "-O2", "-municode", "-mwindows", "-static", "-s",
           "-DUNICODE", "-D_UNICODE", "-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00", "-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS",
           "-I", im, "-I", os.path.join(im, "backends")] + srcs + [
           "-o", out, "-ld3d11", "-ldxgi", "-ld3dcompiler", "-ldwmapi", "-lole32", "-lwindowscodecs",
           "-lcomdlg32", "-lshell32", "-lgdi32", "-luuid", "-lwinhttp"]
    print("build_launcher: compiling (about a minute)...")
    r = subprocess.run(cmd)
    if r.returncode:
        return r.returncode
    print("build_launcher: %s (%d KB)" % (out, os.path.getsize(out) // 1024))
    if a.root_copy:
        shutil.copy2(out, os.path.join(ROOT, "SonicRidersLauncher.exe"))
        print("build_launcher: copied to the repository root")
    return 0


if __name__ == "__main__":
    sys.exit(main())
