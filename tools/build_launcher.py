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
    # the program icon (tools/launcher_icon.py draws it) as resource APPICON
    ico = os.path.join(out_dir, "launcher.ico")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "launcher_icon.py"), ico], check=True)
    rc, res = os.path.join(out_dir, "launcher.rc"), os.path.join(out_dir, "launcher_res.o")
    with open(rc, "w") as f:
        f.write('APPICON ICON "%s"\n' % ico.replace("\\", "/"))
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
