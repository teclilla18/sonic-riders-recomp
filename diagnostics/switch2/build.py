#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build only SR2Diag. Python 3.12+, devkitA64 and switch-tools required.

SDK libraries are isolated in build/switch2-diag/deps. No system SDK mutation,
game configuration, generated game code, external repository or game data is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(args, *, cwd, env, capture=False):
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env,
                            text=True, check=True, capture_output=capture)
    return result.stdout.strip() if capture else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO"), required=not os.environ.get("DEVKITPRO"))
    parser.add_argument("--output", type=Path, default=ROOT / "build/switch2-diag")
    args = parser.parse_args()
    dkp = args.devkitpro.resolve()
    out = args.output.resolve()
    sdk = out / "deps"
    sdk.mkdir(parents=True, exist_ok=True)
    lock = json.loads((HERE / "dependencies.json").read_text())
    archive = sdk / "ounce-devkit-20261117.tar.xz"
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        urllib.request.urlretrieve(lock["sdk"]["url"], temporary)
        temporary.replace(archive)
    if sha256(archive) != lock["sdk"]["sha256"]:
        raise RuntimeError("SDK archive checksum mismatch; refusing to build")
    # Re-extract the verified immutable archive: stale or modified libraries
    # cannot silently become the build input. No package manager is invoked.
    with tarfile.open(archive, "r:xz") as package:
        package.extractall(sdk, filter="data")
    version_file = (sdk / "ounce-devkit-version.txt").read_text()
    if "ounce-devkit 20261117" not in version_file:
        raise RuntimeError("SDK archive version mismatch")
    suffix = ".exe" if os.name == "nt" else ""
    tools = dkp / "devkitA64/bin"
    cc = tools / ("aarch64-none-elf-gcc" + suffix)
    nm = tools / ("aarch64-none-elf-nm" + suffix)
    tool_dir = dkp / "tools/bin"
    elf2nro = tool_dir / ("elf2nro" + suffix)
    nacptool = tool_dir / ("nacptool" + suffix)
    for tool in (cc, nm, elf2nro, nacptool):
        if not tool.is_file():
            raise RuntimeError(f"Missing tool: {tool}")
    env = os.environ.copy()
    # The specs expand DEVKITPRO into a linker-script path. A relative SDK root
    # also works when the worktree's absolute path contains spaces on Windows.
    env["DEVKITPRO"] = "."
    env["DEVKITA64"] = str(dkp / "devkitA64")
    env["PATH"] = os.pathsep.join([str(tools), str(tool_dir), str(dkp / "msys2/usr/bin"), env.get("PATH", "")])
    cc_version = run([cc, "--version"], cwd=sdk, env=env, capture=True).splitlines()[0]
    cpu_text = (sdk / "libounce/switch2_cpu.mk").read_text()
    cpu = re.search(r"^SWITCH2_CPU\s*:=\s*(.+)$", cpu_text, re.M).group(1).split()
    flags = [*cpu, "-mtp=soft", "-fPIE", "-D__SWITCH__", "-D__SWITCH2__", "-D_DEFAULT_SOURCE",
             "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wformat=2",
             "-ffunction-sections", "-fdata-sections", "-fno-common"]
    inputs = sorted((HERE / "src").glob("*")) + [HERE / "build.py", HERE / "dependencies.json", HERE / "README.md", HERE / "THIRD_PARTY_NOTICES.md"]
    source_hashes = {str(p.relative_to(ROOT)).replace("\\", "/"): sha256(p) for p in inputs}
    identity = json.dumps({"sources": source_hashes, "sdk": lock["sdk"]["sha256"], "compiler": cc_version}, sort_keys=True).encode()
    build_id = hashlib.sha256(identity).hexdigest()[:12]
    manifest = {"format": "sr2diag-build-v1", "build_id": build_id, "version": "0.1.0",
                "target": "Switch 2 / kaiHEN / firmware 23.0.1", "dependency_lock": lock,
                "compiler": cc_version, "compile_flags": flags, "source_sha256": source_hashes,
                "tool_sha256": {t.name: sha256(t) for t in (cc, nm, elf2nro, nacptool)},
                "sdk_file_sha256": {p: sha256(sdk / p) for p in (
                    "libounce/lib/libnx.a", "libounce/switch.specs", "libounce/switch.ld",
                    "portlibs/switch2/lib/libvulkan.a")},
                "libraries": ["OunceVK libvulkan", "libounce libnx", "devkitA64 libm/newlib/libgcc"],
                "hardware_tested": False}
    libc = Path(run([cc, "-print-file-name=libc.a"], cwd=sdk, env=env, capture=True))
    if libc.is_file():
        manifest["newlib_archive_sha256"] = sha256(libc)
    (out / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
    romfs = out / "romfs"
    romfs.mkdir(exist_ok=True)
    shutil.copyfile(out / "build-info.json", romfs / "build-info.json")
    components = "; ".join(f"{k}={v}" for k, v in lock["sdk"]["components"].items())
    (out / "build_info.h").write_text(
        f'#define DIAG_BUILD_ID "{build_id}"\n#define DIAG_SDK "20261117"\n#define DIAG_COMPONENTS "{components}"\n')
    objects = []
    include = ["-I" + str(sdk / "portlibs/switch2/include"), "-I" + str(sdk / "libounce/include"), "-I" + str(out)]
    for source in sorted((HERE / "src").glob("*.c")):
        obj = out / (source.stem + ".o")
        print(f"Compiling {source.name}", flush=True)
        run([cc, *flags, *include, "-c", source, "-o", obj], cwd=sdk, env=env)
        objects.append(obj)
    elf = out / "sr2diag.elf"
    print("Linking isolated libounce / OunceVK executable", flush=True)
    run([cc, *cpu, "-mtp=soft", "-fPIE", "-specs=libounce/switch.specs", "-g", *objects,
         "-Lportlibs/switch2/lib", "-Llibounce/lib", "-Wl,-Map," + str(out / "sr2diag.map"),
         "-Wl,--start-group", "-lvulkan", "-lnx", "-lm", "-Wl,--end-group", "-o", elf], cwd=sdk, env=env)
    undefined = run([nm, "--undefined-only", elf], cwd=sdk, env=env, capture=True)
    # Undefined weak optional runtime hooks are permitted; strong imports are not.
    strong = [line for line in undefined.splitlines() if re.search(r"\bU\s", line)]
    if strong:
        raise RuntimeError("Unresolved strong symbols:\n" + "\n".join(strong))
    header = elf.read_bytes()[:64]
    if header[:4] != b"\x7fELF" or struct.unpack_from("<H", header, 18)[0] != 183:
        raise RuntimeError("Output is not AArch64 ELF")
    nacp = out / "sr2diag.nacp"
    run([nacptool, "--create", "SR2Diag - Switch 2 Diagnostics", "Sonic Riders recomp contributors", "0.1.0", nacp], cwd=sdk, env=env)
    nro = out / "sr2diag.nro"
    run([elf2nro, elf, nro, "--nacp=" + str(nacp), "--icon=" + str(sdk / "libounce/default_icon.jpg"),
         "--romfsdir=" + str(romfs)], cwd=sdk, env=env)
    binary = nro.read_bytes()
    if binary[0x10:0x14] != b"NRO0":
        raise RuntimeError("NRO0 header missing")
    ounce_record = binary[:4096].find(b"OUN0")
    if ounce_record < 0 or struct.unpack_from("<II", binary, ounce_record + 4) != (2, 1):
        raise RuntimeError("Native Switch 2 libounce platform record missing")
    package = out / "sd-card/switch/SR2Diag"
    package.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(nro, package / nro.name)
    shutil.copyfile(out / "build-info.json", package / "build-info.json")
    shutil.copyfile(HERE / "README.md", package / "TESTING.txt")
    shutil.copyfile(HERE / "THIRD_PARTY_NOTICES.md", package / "THIRD_PARTY_NOTICES.txt")
    shutil.copytree(sdk / "libounce/licenses", package / "licenses/libounce", dirs_exist_ok=True)
    shutil.copytree(sdk / "portlibs/switch2/share/licenses/ouncevk", package / "licenses/ouncevk", dirs_exist_ok=True)
    if (dkp / "licenses/switch-tools").is_dir():
        shutil.copytree(dkp / "licenses/switch-tools", package / "licenses/switch-tools", dirs_exist_ok=True)
    # A portable SD-card archive contains only the independent diagnostic and
    # metadata/notices, never the ELF, toolchain, intermediate objects or sources.
    shutil.make_archive(str(out / "SR2Diag-Switch2-0.1.0"), "zip", out / "sd-card")
    digest = sha256(nro)
    (out / "sr2diag.nro.sha256").write_text(f"{digest}  sr2diag.nro\n")
    print(f"BUILD_ID {build_id}\nNRO {nro}\nSIZE {len(binary)}\nSHA256 {digest}\nAARCH64_ELF PASS\nNRO_HEADER PASS\nSWITCH2_PLATFORM_RECORD PASS\nSTRONG_IMPORTS PASS", flush=True)


if __name__ == "__main__":
    main()
