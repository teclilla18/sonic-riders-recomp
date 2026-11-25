"""Build the disc-free ARM64 compiler/load probe using the existing Android APK helpers."""

import argparse
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

PROBE = Path(__file__).resolve().parent
sys.path.insert(0, str(PROBE.parents[1]))
import android_build

ROOT = PROBE.parents[2]
WORK = ROOT / "build" / "native-probe"
TCC_REV = "e9e9e7f6236895a32c5e905aada486bab6671e95"
PACKAGE = "net.srnative.sonicriders.nativeprobe"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, required=True, help="Setup tool directory containing SDK, NDK and JDK")
    parser.add_argument("--tinycc", type=Path, default=WORK / "tinycc", help="Clean checkout of the pinned TinyCC revision")
    args = parser.parse_args()

    tcc = args.tinycc.resolve()
    rev = subprocess.check_output(["git", "-C", str(tcc), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(tcc), "status", "--porcelain"], text=True)
    if rev != TCC_REV or dirty:
        parser.error(f"TinyCC must be a clean checkout at {TCC_REV}")

    sdk = args.tools / "android-sdk"
    jdk = str(args.tools / "jdk17")
    ndk = sdk / "ndk" / "27.3.13750724"
    bt = str(sdk / "build-tools" / "35.0.0")
    platform = str(sdk / "platforms" / f"android-{android_build.TARGET_SDK}")
    llvm = ndk / "toolchains" / "llvm" / "prebuilt" / android_build.HOST_TAG / "bin"
    exe = ".exe" if sys.platform == "win32" else ""
    clang = str(llvm / ("clang" + exe))
    tcc_so = WORK / "libtcc.so"
    probe_so = WORK / "libsrnativeprobe.so"
    dex = WORK / "classes.dex"
    key = WORK / "probe-local.keystore"
    apk = WORK / "RidersPlus-Android-Native-Probe-arm64.apk"
    archive = WORK / "tinycc-source.tar.gz"
    converter = WORK / ("tcc-predefs" + exe)
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / "config.h").write_text(
        '#define TCC_VERSION "0.9.28rc"\n'
        "#define TCC_TARGET_ARM64 1\n"
        "#define TARGETOS_ANDROID 1\n"
        "#define CONFIG_TCC_PREDEFS 1\n",
        encoding="ascii",
    )
    host_cc = str(args.tools / "llvm-mingw/bin/x86_64-w64-mingw32-clang.exe") if exe else "cc"
    android_build.run([host_cc, "-DC2STR=1", str(tcc / "conftest.c"), "-o", str(converter)])
    android_build.run([str(converter), str(tcc / "include/tccdefs.h"), str(WORK / "tccdefs_.h")])
    common = [
        clang, f"--target=aarch64-linux-android{android_build.MIN_SDK}", "-shared", "-fPIC",
        "-O2", "-Wl,-z,max-page-size=16384", "-Wl,--no-undefined",
    ]
    android_build.run(common + [
        "-I" + str(WORK), str(tcc / "libtcc.c"), "-Wl,-soname,libtcc.so",
        "-ldl", "-lm", "-o", str(tcc_so),
    ])
    android_build.run(common + [
        "-Wall", "-Wextra", "-Werror", "-I" + str(tcc), str(PROBE / "native_probe.c"),
        str(tcc_so), "-ldl", "-o", str(probe_so),
    ])
    android_build.run(["git", "-C", str(tcc), "archive", "--format=tar.gz", "-o", str(archive), TCC_REV])
    android_build.step_dex(jdk, bt, platform, str(dex), [str(PROBE / "java")])
    android_build.step_keystore(jdk, str(key))
    android_build.step_apk(SimpleNamespace(
        out=str(apk), manifest=str(PROBE / "NativeProbeManifest.xml.in"),
        package=PACKAGE, label="Riders Plus Native Probe", version_code="1", version_name="0.1",
        icon="", build_tools=bt, platform=platform, jdk=jdk, dex=str(dex), keystore=str(key),
        strip=str(llvm / ("llvm-strip" + exe)), lib=[f"arm64-v8a={p}" for p in (probe_so, tcc_so)],
        asset=[str(tcc / "COPYING"), str(archive)],
    ))
    print(apk)


if __name__ == "__main__":
    main()
