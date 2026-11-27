"""Package the disc-free standalone Android proof of concept."""

import argparse
import ast
import hashlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from types import SimpleNamespace
from zipfile import ZIP_DEFLATED, ZipFile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import android_build
from make_icon import png

WORK = ROOT / "build/standalone"
DEPS = WORK / "deps"
DTK_REV = "a0c455e46cab58e1d2e0885623f85089ff0499db"
PACKAGE = "net.srnative.sonicriders.standalone"
MODULE_RECIPE = b"android-arm64-gx-recomp-v1"
# Derived once from the S23-tested v0.1 APK. Keep the original APK as a local test checkpoint.
LEGACY_MODULE_KEY = "3c7dcd45092d2578c4e9ce0c341719b0cea241fa2890ba5a6be92ec238616768"
SIGNER_SHA256 = "988da477c6a0ff9d842bfc8c4f5733bcce97d2deb557c30bc0757a5c1d00c8b8"
SIGNING_PASSWORD = "RIDERSPLUS_STANDALONE_SIGNING_PASSWORD"
NATIVE = {
    "clang": ("clang-21", "libsr_clang.so"),
    "lld": ("lld", "libsr_lld.so"),
}
LIBS = {
    "clang": ("libclang-cpp.so", "libclang-cpp.so"),
    "libllvm": ("libLLVM.so", "libLLVM.so"),
    "libc++": ("libc++_shared.so", "libc++_shared.so"),
    "libffi": ("libffi.so", "libffi.so"),
    "libiconv": ("libiconv.so", "libiconv.so"),
    "libxml2": ("libxml2.so.16.1.4", "libxml2.so.16"),
    "zlib": ("libz.so.1.4", "libz.so.1"),
    "zstd": ("libzstd.so.1.6.0", "libzstd.so.1"),
}
DEB_HASH = {
    "clang": "9fb565013b96ce10abd8f8f217aaebb48f72873f53acbf8886329bcea841a21c",
    "lld": "6299c8fdff59dcff972f6b444805bcdb211c2bd797a64d2ddb3875e699f30630",
    "libllvm": "5056a73f2645fc9c0759e6f94a1bd6edcf36305acaef58dc685211ced9cc2a02",
    "libc++": "53d0b84a7ba7459024257cb94d5b136fe13ef858567f65a8064b35950799f2ca",
    "libffi": "9e13f3b5c40625c384101992bb669a278e07d9e2baa37d286d7ed164288fab1a",
    "libiconv": "a1c6323eff4f660e16ae6f5832356444b3269c19a4f61146355a49e7c4be5ed1",
    "libxml2": "d315861bbcf716cfb088f53cddab9fb6a96430cc66769f79774cc69c2f53bcff",
    "zlib": "e3027565ae50bcd8ba5113c988b589754ae192c9cc3e6afbe3e4ed7c06b2598e",
    "zstd": "c487af7d68664585cc5187330acc7a7a054614cb742fcbb1d8a62e214bb3368a",
}
# 32-bit ARM phones (#33/#45): the same Termux 21.1.8-3 package versions, built for arm.
DEB_HASH_ARM = {
    "clang": "7f3659870dcaed013f084cb718cf569e12fb328199096e0557fc2244283b5310",
    "lld": "29d03be507061b500843359d111437bb03c7c3e718d793bceed61e24642948a2",
    "libllvm": "7612dfb00b1662d9ef57efbe5059eb04594e99dc09ab11c42620b948971bfb97",
    "libc++": "5d4e4dfea77402c0b3ee05b6cb4c73108bb69cc149e2d3a73d4ec6ee6919fd16",
    "libffi": "50ceea8a6739f52a69f8bad09a3c99174fdd0d1f9fcba05d19b109ca39a66812",
    "libiconv": "d0c21741ba47c89d8a86c23cf434376b116d4c0c27834cf35cbe4e35f7523132",
    "libxml2": "0b163b7b9b454a59454018c3fb39fa8d6af46249d44c82450217d9a0ec5e49c9",
    "zlib": "a04f6ce4f36749b49a7ee0432e6fe3f235bdde95ed53f6b9f86138d58fbdc816",
    "zstd": "e74a53b61dadc85844112b401c6999f79aa3d42b970f16dcdc1085c430076251",
}
# Per-ABI inputs. Chaquopy has no 32-bit ARM Python after 3.11, so armeabi-v7a uses 3.11.17-0.
ABIS = {
    "arm64-v8a": SimpleNamespace(
        triple="aarch64-linux-android", clang="aarch64-linux-android30", rust="aarch64-linux-android",
        builtins="aarch64", debs="debs", deb_hash=DEB_HASH, suffix="arm64", recipe=MODULE_RECIPE,
        python=("python-arm64.zip", "c19f39fecada0b3932ab7a61b0f214c1212068cbbae0dc22c2afa38a4f354b8e"),
        stdlib=("python-stdlib.zip", "d5765953d06dc3412c2925d211f188683e1c7936cca985a3091b4183051f0587"),
        libpython="libpython3.13.so"),
    "armeabi-v7a": SimpleNamespace(
        triple="arm-linux-androideabi", clang="armv7a-linux-androideabi30", rust="armv7-linux-androideabi",
        builtins="arm", debs="debs-arm", deb_hash=DEB_HASH_ARM, suffix="armv7",
        recipe=b"android-armv7-gx-recomp-v1",
        python=("python311-armeabi-v7a.zip", "7516b90fcb8a5a2df70e5f9899aab7e63fa43efa0455c3ba7b846ae54d70ee2c"),
        stdlib=("python311-stdlib.zip", "860d5684cfb30cb4b7ff71dd3ba3f9b352e7a91a7329552e1be7ca87a4ae4cf5"),
        libpython="libpython3.11.so"),
}
ABI = ABIS["arm64-v8a"]


def verify(path, digest):
    with path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != digest:
            raise ValueError(f"Unexpected SHA-256 for {path.name}")
    return path


def deb_tar(path):
    with path.open("rb") as stream:
        if stream.read(8) != b"!<arch>\n":
            raise ValueError(f"Unexpected Debian archive format in {path}")
        while header := stream.read(60):
            size = int(header[48:58])
            if header.startswith(b"data.tar"):
                return tarfile.open(fileobj=io.BytesIO(stream.read(size)))
            stream.seek(size + size % 2, io.SEEK_CUR)
    raise ValueError(f"No payload found in {path}")


def package_deb(archive, package):
    path = verify(DEPS / ABI.debs / (package + ".deb"), ABI.deb_hash[package])
    with deb_tar(path) as payload:
        for item in payload:
            if not item.isfile():
                continue
            name = Path(item.name).name
            if package in NATIVE and name == NATIVE[package][0]:
                (WORK / NATIVE[package][1]).write_bytes(payload.extractfile(item).read())
            elif package in LIBS and name == LIBS[package][0]:
                archive.writestr("tool_libs/" + LIBS[package][1], payload.extractfile(item).read())
            elif package == "clang" and "/lib/clang/21/include/" in item.name:
                relative = item.name.split("/lib/clang/21/include/", 1)[1]
                archive.writestr("sdk/clang/" + relative, payload.extractfile(item).read())
            elif "/share/doc/" in item.name:
                archive.writestr(f"notices/{package}/{name}", payload.extractfile(item).read())


def module_key(bundle, native):
    """Fingerprint build inputs, excluding UI, reporting code and notices."""
    digest = hashlib.sha256(ABI.recipe)
    with ZipFile(bundle) as z:
        recipe = ast.parse(z.read("build_on_phone.py"))
        for node in ast.walk(recipe):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "run":
                digest.update(ast.dump(node.args[1]).encode() + b"\0")
        for name in sorted(z.namelist()):
            # rvz2iso.py only turns a picked RVZ/WIA into the ISO before the build; not a build input
            if name in ("build_on_phone.py", "tools/rvz2iso.py") or name.startswith("notices/"):
                continue
            with z.open(name) as entry:
                digest.update(name.encode() + b"\0" + hashlib.file_digest(entry, "sha256").digest())
    for name, data in sorted(native):
        digest.update(name.encode() + b"\0" + hashlib.sha256(data).digest())
    return digest.hexdigest()


def write_release_icon(path):
    """Original lettermark in the Riders Plus website palette."""
    navy = (4, 10, 23, 255)
    white = (233, 244, 255, 255)
    cyan = (63, 227, 255, 255)
    pixels = [[navy] * 128 for _ in range(128)]
    for y in range(32, 96):
        for x in range(24, 108):
            stem = x < 36
            bars = x < 69 and (y < 43 or 56 <= y < 67)
            bowl = 58 <= x < 71 and 37 <= y < 63
            leg = y >= 63 and abs(x - (49 + (y - 63) * 3 // 4)) < 7
            plus = (82 <= x < 107 and 58 <= y < 69) or (89 <= x < 100 and 51 <= y < 76)
            if stem or bars or bowl or leg:
                pixels[y][x] = white
            if plus:
                pixels[y][x] = cyan
    path.write_bytes(png(pixels))


def check_release_paths(paths):
    home = os.environ.get("USERPROFILE") or os.environ.get("HOME")
    if not home:
        return
    prefixes = {home.replace("\\", "/").encode().lower(), home.replace("/", "\\").encode().lower()}
    for path in paths:
        payload = path.read_bytes().lower()
        if any(prefix in payload for prefix in prefixes):
            raise ValueError(f"{path.name} contains a host home path. Rebuild release inputs with path remapping.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--abi", choices=sorted(ABIS), default="arm64-v8a")
    parser.add_argument("--version-code", type=int, default=2)
    parser.add_argument("--version-name", default="0.2")
    parser.add_argument("--qa", action="store_true", help="Build an isolated test package with synthetic storage limits.")
    parser.add_argument("--qa-rebuild", action="store_true", help="Change the QA linker build ID to invalidate the module.")
    parser.add_argument("--qa-remapped", action="store_true", help="Use the beta's remapped native inputs in the QA package.")
    parser.add_argument("--release-keystore", type=Path, help=f"Use the protected standalone signer. Set {SIGNING_PASSWORD}.")
    args = parser.parse_args()
    global ABI
    ABI = ABIS[args.abi]
    t = args.tools.resolve()
    sdk = t / "android-sdk"
    jdk = str(t / "jdk17")
    bt = str(sdk / "build-tools/35.0.0")
    platform = str(sdk / "platforms" / f"android-{android_build.TARGET_SDK}")
    ndk = sdk / "ndk/27.3.13750724/toolchains/llvm/prebuilt" / android_build.HOST_TAG
    exe = ".exe" if sys.platform == "win32" else ""
    sysroot = ndk / "sysroot/usr"
    qualifier = "-QA" if args.qa else "-Beta" if args.release_keystore else ""
    out = WORK / f"RidersPlus-Android-Standalone{qualifier}-v{args.version_name}-{ABI.suffix}.apk"
    assets = WORK / "standalone-tools.zip"
    key = args.release_keystore or WORK / ("qa.keystore" if args.qa else "local.keystore")
    notices = DEPS / "licenses" / "standalone-third-party.zip"
    if args.release_keystore and (args.qa or not notices.is_file() or len(os.environ.get(SIGNING_PASSWORD, "")) < 16):
        parser.error("Release signing requires the dependency bundle and a protected signer password. QA signing stays separate.")
    if (args.qa_rebuild or args.qa_remapped) and not args.qa:
        parser.error("--qa-rebuild and --qa-remapped require --qa")
    if args.version_code < 2 or (not args.qa and not key.is_file()):
        parser.error("Use version code 2 or higher and keep the existing standalone signing key.")
    dex = WORK / "classes.dex"
    dtk = DEPS / "dtk"
    if subprocess.check_output(["git", "-C", str(dtk), "rev-parse", "HEAD"], text=True).strip() != DTK_REV:
        raise ValueError("Unexpected dtk revision")
    remapped = args.release_keystore or args.qa_remapped
    inputs = WORK / "release-inputs" if remapped else ROOT / "build"
    dtk_target = inputs / "rust-target" if remapped else dtk / "target"
    shutil.copyfile(dtk_target / ABI.rust / "release/dtk", WORK / "libsr_dtk.so")
    runtime = inputs / f"android-{args.abi}/runtime.o"
    python_zip = verify(DEPS / ABI.python[0], ABI.python[1])
    stdlib = verify(DEPS / ABI.stdlib[0], ABI.stdlib[1])
    with ZipFile(assets, "w", ZIP_DEFLATED) as z:
        for package in DEB_HASH:
            package_deb(z, package)
        if args.abi == "arm64-v8a":
            z.write(stdlib, "python/stdlib.zip")
        else:  # Chaquopy 3.11 loads zlib from lib-dynload, so zipimport must read the stdlib uncompressed
            stored = io.BytesIO()
            with ZipFile(stdlib) as source, ZipFile(stored, "w") as target:
                for entry in source.infolist():
                    target.writestr(entry.filename, source.read(entry))
            z.writestr("python/stdlib.zip", stored.getvalue())
        with ZipFile(python_zip) as bundle:
            for name in bundle.namelist():
                if name.endswith(".so") and name.startswith(f"jniLibs/{args.abi}/"):
                    z.writestr("tool_libs/" + Path(name).name, bundle.read(name))
                elif name.endswith(".so") and name.startswith(f"lib-dynload/{args.abi}/"):
                    z.writestr("python/dynload/" + Path(name).name, bundle.read(name))
        for name in ("LICENSE-APACHE", "LICENSE-MIT"):
            z.write(dtk / name, "notices/dtk/" + name)
        z.write(runtime, "runtime.o")
        worker = (HERE / "build_on_phone.py").read_bytes()
        if args.abi != "arm64-v8a":  # the recipe is written for arm64; same steps, other target
            worker = worker.replace(b'"--target=aarch64-linux-android30"', f'"--target={ABI.clang}"'.encode(), 1)
            worker = worker.replace(b'"include/aarch64-linux-android"', f'"include/{ABI.triple}"'.encode(), 1)
        if args.qa_rebuild:
            worker = worker.replace(b'"--build-id=sha1"', b'"--build-id=md5"', 1)
        z.writestr("build_on_phone.py", worker)
        for name in ("config.yml", "hle.txt", "patches.txt", "hot_functions.txt"):
            path = ROOT / "config/GXEE8P" / name
            if path.is_file():
                z.write(path, f"config/GXEE8P/{name}")
        for name in ("extract_iso.py", "rvz2iso.py", "gcbin.py", "recomp/gcrecomp.py", "recomp/ppc2c.py",
                     "recomp/crlive.py", "recomp/leaflocal.py", "recomp/singleflow.py"):
            z.write(ROOT / "tools" / name, "tools/" + name)
        for directory in ("runtime/include", "runtime/src"):
            for header in (ROOT / directory).glob("*.h"):
                z.write(header, "include/" + header.name)
        for header in (sysroot / "include").rglob("*.h"):
            path = header.relative_to(sysroot / "include")
            if not path.parts[0].endswith("-linux-android") or path.parts[0] == ABI.triple:
                z.write(header, "sdk/include/" + path.as_posix())
        link_dir = sysroot / "lib" / ABI.triple / "30"
        for name in ("crtbegin_so.o", "crtend_so.o", "libvulkan.so", "libandroid.so", "liblog.so",
                     "libdl.so", "libm.so", "libc.so"):
            z.write(link_dir / name, "sdk/link/" + name)
        builtins = next(ndk.glob(f"lib/clang/*/lib/linux/libclang_rt.builtins-{ABI.builtins}-android.a"))
        z.write(builtins, "sdk/link/libbuiltins.a")

    python = WORK / "libsr_python.so"
    with ZipFile(python_zip) as bundle:
        python_so = WORK / ABI.libpython
        python_so.write_bytes(bundle.read(f"jniLibs/{args.abi}/{ABI.libpython}"))
    android_build.run([
        str(ndk / "bin" / ("clang" + exe)), f"--target={ABI.clang}",
        "-Wl,-z,max-page-size=16384", str(HERE / "python_main.c"),
        # Chaquopy's 3.11 library has no SONAME: link it by name, or the host path becomes its NEEDED entry
        *([str(python_so)] if args.abi == "arm64-v8a" else ["-L", str(WORK), "-l" + ABI.libpython[3:-3]]),
        "-o", str(python),
    ])
    libs = inputs / f"android/sdl/{args.abi}/libs/{args.abi}"
    apk_libs = [libs / "libSDL2.so", libs / "libc++_shared.so", python, WORK / "libsr_dtk.so"]
    apk_libs.extend(WORK / target for _, target in NATIVE.values())
    stripped = WORK / "apk-libs"
    stripped.mkdir(exist_ok=True)
    strip = str(ndk / "bin" / ("llvm-strip" + exe))
    for library in apk_libs:
        android_build.run([strip, "--strip-debug", "-o", str(stripped / library.name), str(library)])
    apk_libs = [stripped / library.name for library in apk_libs]
    if remapped:
        check_release_paths([runtime, *apk_libs])
    metadata = WORK / "installer.properties"
    current_key = module_key(assets, [(p.name, p.read_bytes()) for p in apk_libs])
    # the tools folder is re-extracted when this changes: it covers the files left out of the module key too
    converter = (ROOT / "tools/rvz2iso.py").read_bytes()
    bundle_key = hashlib.sha256(current_key.encode() + b"\0" + worker + b"\0" + converter).hexdigest()
    metadata.write_text(f"module={current_key}\nlegacy={LEGACY_MODULE_KEY}\nbundle={bundle_key}\n"
                        f"version={args.version_name} ({args.version_code})\nabi={args.abi}\n", encoding="ascii")
    android_build.step_dex(jdk, bt, platform, str(dex), [
        str(HERE / "java"), str(ROOT / "android/java"), str(t / "sdl2-src/android-project/app/src/main/java"),
    ])
    if args.qa and not key.is_file():
        android_build.step_keystore(jdk, str(key))
    icon = ROOT / "setup/icon.png"
    if args.release_keystore:
        icon = WORK / "standalone-release-icon.png"
        write_release_icon(icon)
    android_build.step_apk(SimpleNamespace(
        out=str(out), manifest=str(HERE / "AndroidManifest.xml.in"),
        package=PACKAGE + (".qa" if args.qa else ""), label="Riders Rebuilt QA" if args.qa else "Riders Rebuilt",
        version_code=str(args.version_code), version_name=args.version_name,
        icon=str(icon), build_tools=bt, platform=platform,
        jdk=jdk, dex=str(dex), keystore=str(key), strip="",
        password=f"env:{SIGNING_PASSWORD}" if args.release_keystore else "pass:android",
        lib=[f"{args.abi}={path}" for path in apk_libs],
        asset=[str(assets), str(metadata)] + ([str(notices)] if args.release_keystore else []),
    ))
    if args.release_keystore:
        signatures = subprocess.check_output([
            android_build.bat(str(Path(bt) / "apksigner")), "verify", "--print-certs", str(out),
        ], env=android_build.java_env(jdk), text=True)
        if f"Signer #1 certificate SHA-256 digest: {SIGNER_SHA256}" not in signatures:
            out.unlink()
            raise ValueError("Release signing certificate does not match the existing standalone package.")
    print(f"Prepared module compatible with original PoC: {current_key == LEGACY_MODULE_KEY}")
    print(out)


if __name__ == "__main__":
    main()
