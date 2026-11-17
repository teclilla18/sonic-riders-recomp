#!/usr/bin/env python3
"""Isolated native Switch 2 build graph and packaging. Other targets are unchanged."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build/switch2"


def digest(path):
    with Path(path).open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def quote(value):
    value = str(value)
    return subprocess.list2cmdline([value]) if os.name == "nt" else shlex.quote(value)


def command(items):
    return " ".join(quote(x) for x in items)


def executable(path):
    path = Path(path)
    return path.with_suffix(".exe") if os.name == "nt" else path


def configure(args, root):
    from ninja_syntax import Writer
    from write_version import write as version_write
    root = Path(root)
    dkp = Path(args.devkitpro or os.environ.get("DEVKITPRO", "")).resolve()
    sdk = Path(args.ounce_sdk or root / "build/switch2-diag/deps").resolve()
    cc = executable(dkp / "devkitA64/bin/aarch64-none-elf-gcc")
    cxx = executable(dkp / "devkitA64/bin/aarch64-none-elf-g++")
    glslc = Path(args.glslc or shutil.which("glslc") or "")
    if not cc.is_file() or not cxx.is_file():
        raise SystemExit("switch2: --devkitpro must contain devkitA64")
    if not glslc.is_file():
        raise SystemExit("switch2: provide --glslc with the installed shaderc executable")
    lock = json.loads((root / "diagnostics/switch2/dependencies.json").read_text())
    archive = sdk / "ounce-devkit-20261117.tar.xz"
    if not archive.is_file() or digest(archive) != lock["sdk"]["sha256"]:
        raise SystemExit("switch2: verified Phase 1 SDK archive missing/mismatched; use diagnostics/switch2/build.py first")
    if "ounce-devkit 20261117" not in (sdk / "ounce-devkit-version.txt").read_text():
        raise SystemExit("switch2: wrong SDK version")
    # Compare linked files against the immutable archive, not just its label.
    import tarfile
    with tarfile.open(archive, "r:xz") as tar:
        for name in ("libounce/lib/libnx.a", "libounce/switch.specs", "libounce/switch.ld", "portlibs/switch2/lib/libvulkan.a"):
            member = tar.extractfile(name)
            if member is None or hashlib.sha256(member.read()).hexdigest() != digest(sdk / name):
                raise SystemExit("switch2: modified SDK input: " + name)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "obj").mkdir(exist_ok=True)
    (OUT / "gen").mkdir(exist_ok=True)
    gen = root / "build/gen"
    if not list(gen.glob("code_*.c")):
        print("switch2: generating own-disc C code locally", flush=True)
        subprocess.run([sys.executable, "tools/recomp/gcrecomp.py", "config/GXEE8P", "baserom", "build/gen"], cwd=root, check=True)
    imgui = Path(args.imgui or root / "build/ext/imgui-1.92.9b")
    imgui_local = root / "build/ext/imgui-1.92.9b"
    if not (imgui / "imgui.h").is_file():
        raise SystemExit("switch2: --imgui must point to the existing ImGui1.92.9b source")
    if imgui.resolve() != imgui_local.resolve():
        imgui_local.mkdir(parents=True, exist_ok=True)
        for name in ("LICENSE.txt", "imconfig.h", "imgui.cpp", "imgui.h", "imgui_draw.cpp", "imgui_internal.h", "imgui_tables.cpp",
                     "imgui_widgets.cpp", "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h"):
            shutil.copyfile(imgui / name, imgui_local / name)
    import re
    cpu = re.search(r"^SWITCH2_CPU\s*:=\s*(.+)$", (sdk / "libounce/switch2_cpu.mk").read_text(), re.M).group(1).split()
    # Keep the regular guest-base path for initial integration. No unverified
    # fixed-register optimization or Switch1 clock profile is inherited.
    flags = cpu + ["-mtp=soft", "-fPIE", "-D__SWITCH__", "-D__SWITCH2__", "-D_DEFAULT_SOURCE",
                   "-ffunction-sections", "-fdata-sections", "-fno-diagnostics-show-caret"]
    include = ["-Iruntime/include", "-Iruntime/src", "-Ibuild/gen", "-Ibuild/switch2/gen",
               "-Ibuild/ext/imgui-1.92.9b", "-DIMGUI_DISABLE_DEMO_WINDOWS", "-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS",
               "-I" + str(sdk / "portlibs/switch2/include"), "-I" + str(sdk / "libounce/include")]
    runtime = sorted((root / "runtime/src").glob("*.c")) + sorted((root / "runtime/src").glob("*.cpp"))
    runtime += sorted((root / "runtime/src/platform/switch2").glob("*.c"))
    runtime += [root / "runtime/src/platform/switch/input_hid.c", root / "runtime/src/platform/android/render_vk.c"]
    version = OUT / "gen/version.c"
    version_write(str(version))
    runtime += [version]
    generated = sorted(gen.glob("code_*.c")) + [gen / "table.c"]
    relative = lambda p: Path(p).relative_to(root).as_posix()
    config = {"root": str(root), "sdk": str(sdk), "devkitpro": str(dkp), "cc": str(cc), "cxx": str(cxx),
              "glslc": str(glslc.resolve()), "cpu": cpu, "flags": flags, "dependency_lock": lock,
              "own_sources": [relative(p) for p in runtime if not relative(p).startswith("build/")] +
                  [relative(p) for p in (root / "runtime/include").rglob("*.h")] +
                  [relative(p) for p in (root / "runtime/src").rglob("*.h")] +
                  [relative(p) for p in (root / "runtime/shaders").glob("*") if p.is_file()] +
                  ["configure.py", "tools/switch2_build.py", "config/GXEE8P/hle.txt", "config/GXEE8P/patches.txt"]}
    (OUT / "build-config.json").write_text(json.dumps(config, indent=2) + "\n")
    py = quote(sys.executable)
    with (root / "build.ninja").open("w") as f:
        n = Writer(f)
        n.variable("ninja_required_version", "1.10")
        n.variable("cc", quote(cc)); n.variable("cxx", quote(cxx))
        n.variable("cflags", command(flags + include + ["-std=gnu11", "-g"]))
        n.variable("cxxflags", command(flags + include + ["-std=gnu++20", "-g", "-fno-rtti", "-fno-exceptions"]))
        n.rule("cc", "$cc -MMD -MF $out.d $cflags $extra -c $in -o $out", depfile="$out.d", deps="gcc", description="CC $in")
        n.rule("cxx", "$cxx -MMD -MF $out.d $cxxflags $extra -c $in -o $out", depfile="$out.d", deps="gcc", description="CXX $in")
        inputs = ["tools/recomp/gcrecomp.py", "tools/recomp/ppc2c.py", "tools/recomp/crlive.py", "tools/recomp/leaflocal.py",
                  "tools/recomp/singleflow.py", "tools/gcbin.py", "config/GXEE8P/symbols.txt", "config/GXEE8P/_Main/symbols.txt",
                  "config/GXEE8P/hle.txt", "config/GXEE8P/patches.txt", "config/GXEE8P/hot_functions.txt"]
        inputs = [p for p in inputs if (root / p).exists()]
        n.rule("recomp", py + " tools/recomp/gcrecomp.py config/GXEE8P baserom build/gen", description="RECOMP", restat=True, generator=True, pool="console")
        n.build([relative(p) for p in generated] + ["build/gen/funcs.h"], "recomp", inputs)
        n.rule("version", py + " tools/switch2_build.py version", description="VERSION native Switch2", restat=True)
        n.build("version_always", "phony")
        n.build(relative(version), "version", implicit=["version_always"])
        shaders = [relative(p) for p in (root / "runtime/shaders").glob("*") if p.is_file()]
        header = "build/switch2/gen/gx_shaders_spv.h"
        n.rule("shaders", py + " tools/embed_shaders.py spv $out " + quote(glslc.resolve()) + " build/switch2/shaders", description="SPIRV $out")
        n.build(header, "shaders", shaders + ["tools/embed_shaders.py"])
        keys = ["config/GXEE8P/shader_keys.txt"]
        spec_header = "build/switch2/gen/gx_spec_spv.h"
        n.rule("spec", py + " tools/gen_shaders.py --vk $out " + quote(glslc.resolve()) + " build/switch2/spec " + " ".join(keys), description="SPECIALIZED SPIRV", restat=True)
        n.build(spec_header, "spec", shaders + ["tools/gen_shaders.py", "tools/embed_shaders.py"] + keys)
        objects = []
        for p in generated + runtime:
            src = relative(p)
            obj = "build/switch2/obj/" + src.replace("/", "_") + ".o"
            extra = "-O2 -ffp-contract=off -fno-strict-aliasing -fwrapv -w" if p in generated else "-O2 -Wall"
            n.build(obj, "cxx" if p.suffix == ".cpp" else "cc", src,
                    implicit=["build/gen/funcs.h", header, spec_header] if p not in generated else None, variables={"extra": extra})
            objects.append(obj)
        for name in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"):
            obj = "build/switch2/obj/imgui_" + name + ".o"
            n.build(obj, "cxx", "build/ext/imgui-1.92.9b/" + name, variables={"extra": "-O2 -w"})
            objects.append(obj)
        n.rule("link", py + " tools/switch2_build.py link --objects $in", description="LINK native Switch2")
        elf = "build/switch2/sonicriders-switch2.elf"
        n.build(elf, "link", objects, implicit=["tools/switch2_build.py", "build/switch2/build-config.json"])
        n.rule("nro", py + " tools/switch2_build.py pack", description="NRO native Switch2")
        n.build("build/switch2/sonicriders-switch2.nro", "nro", elf, implicit=["tools/switch2_build.py", "build/switch2/build-config.json"])
        n.build("gen_objs", "phony", objects[:len(generated)])
        n.default("build/switch2/sonicriders-switch2.nro")
    print(f"wrote build.ninja (switch2): {len(generated)} generated + {len(runtime)} runtime sources; verified SDK20261117", flush=True)


def read_config():
    return json.loads((OUT / "build-config.json").read_text())


def identity(c):
    source_hashes = {p: digest(ROOT / p) for p in c["own_sources"]}
    versions = [subprocess.check_output([c[t], "--version"], text=True).splitlines()[0] for t in ("cc", "glslc")]
    fields = {"sources": source_hashes, "compiler": versions[0], "shader_compiler": versions[1],
              "sdk": c["dependency_lock"]["sdk"]["sha256"], "flags": c["flags"]}
    build_id = hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()[:12]
    return build_id, source_hashes, versions


def version():
    from write_version import build_version
    c = read_config()
    build_id, _, _ = identity(c)
    content = "const char g_build_version[] = " + json.dumps("Switch2-phase2 " + build_version() + " src:" + build_id) + ";\n"
    path = OUT / "gen/version.c"
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def link(objects):
    c = read_config()
    sdk = Path(c["sdk"])
    env = os.environ.copy()
    env["DEVKITPRO"] = "."  # relative specs expansion works in paths containing spaces
    env["PATH"] = str(Path(c["devkitpro"]) / "devkitA64/bin") + os.pathsep + env.get("PATH", "")
    flags = c["cpu"] + ["-mtp=soft", "-fPIE", "-g", "-specs=libounce/switch.specs",
        "-Lportlibs/switch2/lib", "-Llibounce/lib", "-Wl,-Map," + str(OUT / "sonicriders-switch2.map"),
        "-Wl,--wrap=pthread_cond_wait", "-Wl,--wrap=pthread_cond_timedwait", "-Wl,--wrap=thrd_sleep"]
    response = OUT / "objects.rsp"
    response.write_text("\n".join('"' + (ROOT / p).as_posix() + '"' for p in objects) + "\n")
    subprocess.run([c["cxx"], *flags, "@" + str(response),
                    "-Wl,--start-group", "-lvulkan", "-lstdc++", "-lnx", "-lm", "-Wl,--end-group",
                    "-o", str(OUT / "sonicriders-switch2.elf")], cwd=sdk, env=env, check=True)


def pack():
    c = read_config()
    dkp = Path(c["devkitpro"])
    elf = OUT / "sonicriders-switch2.elf"
    nm = executable(dkp / "devkitA64/bin/aarch64-none-elf-nm")
    symbols = subprocess.check_output([str(nm), "--undefined-only", str(elf)], text=True)
    if any(" U " in s for s in symbols.splitlines()):
        raise SystemExit("switch2: ELF contains unresolved strong symbols")
    build_id, source_hashes, versions = identity(c)
    if build_id not in (OUT / "gen/version.c").read_text():
        raise SystemExit("switch2: runtime version and source identity disagree; rebuild")
    manifest = {"format": "sonicriders-switch2-phase2-v1", "hardware_tested": False,
                "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "compiler": versions[0],
                "shader_compiler": versions[1],
                "cpu_flags": c["flags"], "dependencies": c["dependency_lock"],
                "source_sha256": source_hashes,
                "guest_memory": {"strategy": "svcMapMemory stack", "reservation": 0x20010000, "backed": [0x1c00000, 0x10000]},
                "elf_sha256": digest(elf)}
    imgui = ROOT / "build/ext/imgui-1.92.9b"
    manifest["imgui"] = {"version": "1.92.9b", "files_sha256": {p.name: digest(p) for p in imgui.iterdir() if p.is_file()}}
    manifest["build_id"] = build_id
    (OUT / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
    romfs = OUT / "romfs"
    romfs.mkdir(exist_ok=True)
    shutil.copyfile(OUT / "build-info.json", romfs / "build-info.json")
    nacp = OUT / "sonicriders-switch2.nacp"
    subprocess.run([str(executable(dkp / "tools/bin/nacptool")), "--create", "Sonic Riders - Switch 2 Experimental",
                    "native port (own disc image required)", "0.1.0", str(nacp)], check=True)
    nro = OUT / "sonicriders-switch2.nro"
    subprocess.run([str(executable(dkp / "tools/bin/elf2nro")), str(elf), str(nro), "--nacp=" + str(nacp),
                    "--icon=" + str(Path(c["sdk"]) / "libounce/default_icon.jpg"), "--romfsdir=" + str(romfs)], check=True)
    binary = nro.read_bytes()
    record = binary[:4096].find(b"OUN0")
    if binary[16:20] != b"NRO0" or record < 0 or struct.unpack_from("<II", binary, record + 4) != (2, 1):
        raise SystemExit("switch2: native NRO platform/header verification failed")
    (OUT / "sonicriders-switch2.nro.sha256").write_text(digest(nro) + "  " + nro.name + "\n")
    print(f"Native Switch2 NRO built; id={manifest['build_id']} bytes={len(binary)}; hardware testing pending", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["version", "link", "pack"])
    parser.add_argument("--objects", nargs="*", default=[])
    a = parser.parse_args()
    if a.action == "version": version()
    elif a.action == "link": link(a.objects)
    else: pack()
