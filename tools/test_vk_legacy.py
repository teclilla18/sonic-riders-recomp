#!/usr/bin/env python3
"""Compile/run the renderer's actual pass/feature/pool helpers against checked Vulkan mocks.

Requires a C11 compiler and Vulkan headers (Android NDK headers work on desktop).
No disc inputs or generated game code are read. --android builds a PIE to run with adb.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"static\s+[^;{}]+\b" + re.escape(name) + r"\([^;{}]*\)\s*(?:/\*.*?\*/\s*)?\{", source)
    if not match:
        raise ValueError("function not found: " + name)
    start = match.start()
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cc", default=shutil.which("clang") or shutil.which("cc"))
    ap.add_argument("--vulkan-include", required=True)
    ap.add_argument("--android", action="store_true")
    a = ap.parse_args()
    if not a.cc:
        ap.error("a C11 compiler is required")
    source = (ROOT / "runtime/src/platform/android/render_vk.c").read_text()
    start = source.index("typedef struct LegacyPass")
    end = source.index("/* ---- end legacy rendering objects ---- */", start)
    pool_start = source.index("typedef struct Block")
    pool_end = source.index("static void image_make", pool_start)
    code = source[start:end] + function(source, "dynamic_supported") + source[pool_start:pool_end]
    code += "enum { TGT_EFB, TGT_TEX, TGT_SWAP };\n" + function(source, "pipe_build")
    for name in ("src1_ok", "gx_pipe_make", "state_key"):
        code += function(source, name)
    recording = source.index("static int g_in_efb;")
    code += source[recording:source.index("/* one screen-space quad", recording)]
    out = ROOT / "build/tests/vk-legacy"
    out.mkdir(parents=True, exist_ok=True)
    # Do not put an NDK sysroot ahead of the desktop compiler's libc headers.
    include = out / "include"
    for name in ("vulkan", "vk_video"):
        if (Path(a.vulkan_include) / name).is_dir():
            shutil.copytree(Path(a.vulkan_include) / name, include / name, dirs_exist_ok=True)
    (out / "vk_legacy_under_test.h").write_text(code)
    exe = out / ("test-vk-legacy" if a.android else "test-vk-legacy.exe" if os.name == "nt" else "test-vk-legacy")
    cmd = [a.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
           "-Wno-unused-function", "-Wno-missing-field-initializers", "-I" + str(include),
           "-I" + str(out), "-I" + str(ROOT / "runtime/src")]
    if a.android:
        cmd += ["--target=aarch64-linux-android30", "-fPIE", "-pie"]
    elif os.name == "nt":
        cmd += ["-I" + str(ROOT / "runtime/include/compat")]
    else:
        cmd += ["-pthread"]
    cmd += [str(ROOT / "tools/test_vk_legacy.c"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    # Every pass end must go through the shared dispatcher, across copies/effects/overlay/touch.
    assert source.count("p_end_rendering(g_cmd)") == 1
    assert source.count("p_begin_rendering(g_cmd") == 2
    assert "legacy_forget_view(t->view);" in function(source, "image_free")
    assert "legacy_forget_view(t->view);" in function(source, "img_destroy")
    assert "legacy_forget_view(g_sview[i]);" in function(source, "swap_destroy")
    if not a.android:
        subprocess.run([str(exe)], check=True)
    print("Vulkan legacy regression executable:", exe)


if __name__ == "__main__":
    main()
