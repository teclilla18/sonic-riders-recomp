#!/usr/bin/env python3
"""Android build (docs/ANDROID_PLAN.md): build.ninja for `configure.py --target android`, and the build
steps ninja runs through this script (Windows .bat/.cmd tools, several commands per step):

  android_build.py sdl      NDK SDL_SRC ABI OUTDIR          SDL2 (libSDL2.so, libc++_shared.so) with ndk-build
  android_build.py dex      JDK BUILD_TOOLS PLATFORM OUT.dex SRCDIR...   javac + d8
  android_build.py keystore JDK KEYSTORE                    a local signing key, made once (never in git)
  android_build.py apk      ...                             manifest + dex + .so -> aligned, signed APK

No Gradle: the APK is assembled with the SDK's aapt2, zipalign and apksigner. Everything this produces
holds (or will hold) recompiled game code or a signing key: it stays in build/ and the tools folder.
"""
import glob
import os
import shutil
import subprocess
import sys
import zipfile

HOST_TAG = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
MIN_SDK = 29     # Android 10: Vulkan 1.1, AAudio
TARGET_SDK = 35
ABIS = {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android"}
JAVA_PACKAGE_DIR = "android/java"


def run(cmd, env=None, cwd=None):
    print(" ".join('"%s"' % c if " " in c else c for c in cmd), flush=True)
    r = subprocess.run(cmd, env=env, cwd=cwd)
    if r.returncode:
        sys.exit("android_build: %s failed (exit code %d)" % (os.path.basename(cmd[0]), r.returncode))


def java_env(jdk):
    env = dict(os.environ)
    env["JAVA_HOME"] = jdk
    env["PATH"] = os.path.join(jdk, "bin") + os.pathsep + env.get("PATH", "")
    return env


def bat(path):
    """an SDK script: name.bat on Windows, name elsewhere"""
    return path + ".bat" if os.name == "nt" else path


# ---------------------------------------------------------------- steps
def step_sdl(ndk, sdl_src, abi, outdir):
    """SDL2's own Android.mk (with hidapi, so C++: the shared libc++ goes into the APK too)"""
    ndk_build = os.path.join(ndk, "ndk-build.cmd" if os.name == "nt" else "ndk-build")
    run([ndk_build, "NDK_PROJECT_PATH=null", "APP_BUILD_SCRIPT=" + os.path.join(sdl_src, "Android.mk"),
         "APP_ABI=" + abi, "APP_PLATFORM=android-%d" % MIN_SDK, "APP_STL=c++_shared", "APP_OPTIM=release",
         "APP_SUPPORT_FLEXIBLE_PAGE_SIZES=true", "NDK_OUT=" + os.path.join(outdir, "obj"),
         "NDK_LIBS_OUT=" + os.path.join(outdir, "libs"), "-j%d" % (os.cpu_count() or 4)])
    for lib in ("libSDL2.so", "libc++_shared.so"):
        if not os.path.isfile(os.path.join(outdir, "libs", abi, lib)):
            sys.exit("android_build: ndk-build made no %s" % lib)


def step_dex(jdk, build_tools, platform, out, srcdirs):
    work = out + ".work"
    shutil.rmtree(work, ignore_errors=True)
    classes = os.path.join(work, "classes")
    os.makedirs(classes)
    srcs = sorted(p for d in srcdirs for p in glob.glob(os.path.join(d, "**", "*.java"), recursive=True))
    android_jar = os.path.join(platform, "android.jar")
    env = java_env(jdk)
    listfile = os.path.join(work, "sources.txt")
    with open(listfile, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % s.replace("\\", "/") for s in srcs))
    run([os.path.join(jdk, "bin", "javac"), "-nowarn", "-Xlint:none", "-encoding", "UTF-8", "-source", "11", "-target", "11",
         "-classpath", android_jar, "-d", classes, "@" + listfile], env=env)
    jar = os.path.join(work, "classes.jar")
    run([os.path.join(jdk, "bin", "jar"), "cf", jar, "-C", classes, "."], env=env)
    run([bat(os.path.join(build_tools, "d8")), "--release", "--min-api", str(MIN_SDK), "--lib", android_jar,
         "--output", work, jar], env=env)
    shutil.move(os.path.join(work, "classes.dex"), out)
    shutil.rmtree(work, ignore_errors=True)


def step_keystore(jdk, keystore):
    """one local key per tools folder: Android only installs an update signed with the same key, and
    uninstalling (to change keys) deletes the app's folder with the disc image and saves in it"""
    if os.path.isfile(keystore):
        os.utime(keystore)
        return
    os.makedirs(os.path.dirname(os.path.abspath(keystore)), exist_ok=True)
    run([os.path.join(jdk, "bin", "keytool"), "-genkeypair", "-keystore", keystore, "-storetype", "PKCS12",
         "-alias", "local", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-storepass", "android",
         "-keypass", "android", "-dname", "CN=Sonic Riders local build"], env=java_env(jdk))


def step_apk(a):
    work = a.out + ".work"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    with open(a.manifest, encoding="utf-8") as f:
        text = f.read()
    for k, v in (("@PACKAGE@", a.package), ("@LABEL@", a.label), ("@VERSION_CODE@", a.version_code),
                 ("@VERSION_NAME@", a.version_name)):
        text = text.replace(k, v)
    man = os.path.join(work, "AndroidManifest.xml")
    with open(man, "w", encoding="utf-8") as f:
        f.write(text)
    base = os.path.join(work, "base.apk")
    tools = a.build_tools
    run([os.path.join(tools, "aapt2"), "link", "-o", base, "--manifest", man, "-I", os.path.join(a.platform, "android.jar"),
         "--min-sdk-version", str(MIN_SDK), "--target-sdk-version", str(TARGET_SDK)])
    with zipfile.ZipFile(base, "a") as z:
        z.write(a.dex, "classes.dex", compress_type=zipfile.ZIP_DEFLATED)
        for spec in a.lib:  # ABI=FILE; stored, so they load straight from the APK (extractNativeLibs false)
            abi, path = spec.split("=", 1)
            z.write(path, "lib/%s/%s" % (abi, os.path.basename(path)), compress_type=zipfile.ZIP_STORED)
    aligned = os.path.join(work, "aligned.apk")
    run([os.path.join(tools, "zipalign"), "-f", "-P", "16", "4", base, aligned])
    run([bat(os.path.join(tools, "apksigner")), "sign", "--ks", a.keystore, "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", a.out, aligned], env=java_env(a.jdk))
    shutil.rmtree(work, ignore_errors=True)


# ---------------------------------------------------------------- build.ninja
def write_ninja(args, root, writer_cls, py):
    """configure.py --target android: milestone 1 builds the probe APK (tools/android/probe, no game code)"""
    def need(path, what):
        if not os.path.exists(path):
            sys.exit("configure: %s not found: %s" % (what, path))
        return path.replace("\\", "/")
    sdk = need(args.android_sdk or os.environ.get("ANDROID_HOME", ""), "Android SDK (--android-sdk)")
    ndk = args.ndk or (sorted(glob.glob(os.path.join(sdk, "ndk", "*")))[-1:] or [""])[0]
    ndk = need(ndk, "Android NDK (--ndk)")
    jdk = need(args.jdk or os.environ.get("JAVA_HOME", ""), "JDK 17 (--jdk)")
    sdl_src = need(args.sdl_src, "SDL2 source (--sdl-src)")
    bt = need((sorted(glob.glob(os.path.join(sdk, "build-tools", "*")))[-1:] or [""])[0], "SDK build-tools")
    plat = need(os.path.join(sdk, "platforms", "android-%d" % TARGET_SDK), "SDK platform android-%d" % TARGET_SDK)
    keystore = (args.keystore or os.path.join(os.path.dirname(os.path.abspath(sdk)), "sonicriders-local.keystore")).replace("\\", "/")
    for p in (sdk, ndk, jdk, sdl_src, keystore):
        if " " in p:
            sys.exit("configure: Android tool paths must not contain spaces (ndk-build): %s" % p)
    abis = list(ABIS) if args.abi == "both" else [args.abi]
    llvm = "%s/toolchains/llvm/prebuilt/%s/bin" % (ndk, HOST_TAG)
    exe = ".exe" if os.name == "nt" else ""
    glslc = "%s/shader-tools/%s/glslc%s" % (ndk, HOST_TAG, exe)
    out = "build/android"
    me = "tools/android_build.py"
    with open(os.path.join(root, "build.ninja"), "w") as f:
        n = writer_cls(f)
        n.variable("ninja_required_version", "1.10")
        n.rule("sdl", "%s %s sdl %s %s $abi $outdir" % (py, me, ndk, sdl_src), description="SDL2 ($abi, ndk-build)", pool="console")
        n.rule("cc", "%s/clang%s --target=${triple}%d -MMD -MF $out.d -fPIC -O2 -g -Wall $cflags -c $in -o $out" % (llvm, exe, MIN_SDK),
               depfile="$out.d", deps="gcc", description="CC $in")
        n.rule("so", "%s/clang%s --target=${triple}%d -shared -o $out $in -Wl,-soname,libmain.so -Wl,--no-undefined "
               "-Wl,-z,max-page-size=16384 -Wl,--build-id=sha1 $ldflags" % (llvm, exe, MIN_SDK), description="LINK $out")
        n.rule("spv", "%s -mfmt=c -O --target-env=vulkan1.1 -o $out $in" % glslc, description="GLSLC $in")
        n.rule("dex", "%s %s dex %s %s %s $out %s $srcdirs" % (py, me, jdk, bt, plat, JAVA_PACKAGE_DIR), description="DEX $out")
        n.rule("keystore", "%s %s keystore %s $out" % (py, me, jdk), description="KEYSTORE (local signing key)")
        n.rule("apk", "%s %s apk --jdk %s --build-tools %s --platform %s --manifest android/AndroidManifest.xml.in "
               "--package $package --label \"$label\" --version-code $vcode --version-name $vname --dex $dex --keystore %s $libs --out $out"
               % (py, me, jdk, bt, plat, keystore), description="APK $out")
        n.build(keystore, "keystore")
        sdl_java = sdl_src + "/android-project/app/src/main/java"
        java_srcs = sorted(p.replace("\\", "/") for d in (sdl_java, os.path.join(root, JAVA_PACKAGE_DIR))
                           for p in glob.glob(os.path.join(d, "**", "*.java"), recursive=True))
        java_srcs = [os.path.relpath(p, root).replace("\\", "/") if p.startswith(root.replace("\\", "/")) else p for p in java_srcs]
        dex = out + "/classes.dex"
        n.build(dex, "dex", java_srcs + [me], variables={"srcdirs": sdl_java})
        gen = out + "/probe/gen"
        incs = []
        for stage in ("vert", "frag"):
            o = "%s/probe_%s.inc" % (gen, stage)
            n.build(o, "spv", "tools/android/probe/probe.%s" % stage)
            incs.append(o)
        defaults = []
        for abi in abis:
            triple = ABIS[abi]
            sdl_out = "%s/sdl/%s" % (out, abi)
            sdl_libs = ["%s/libs/%s/libSDL2.so" % (sdl_out, abi), "%s/libs/%s/libc++_shared.so" % (sdl_out, abi)]
            n.build(sdl_libs, "sdl", [me], variables={"abi": abi, "outdir": sdl_out})
            obj = "%s/probe/%s/probe.o" % (out, abi)
            n.build(obj, "cc", "tools/android/probe/probe.c", implicit=incs,
                    variables={"triple": triple, "cflags": "-I%s/include -I%s" % (sdl_src, gen)})
            so = "%s/probe/%s/libmain.so" % (out, abi)
            n.build(so, "so", [obj], implicit=sdl_libs[:1],
                    variables={"triple": triple, "ldflags": "-L%s/libs/%s -lSDL2 -lvulkan -lGLESv3 -lEGL -llog -landroid -ldl -lm" % (sdl_out, abi)})
            apk = "%s/probe-%s.apk" % (out, abi)
            libs = " ".join("--lib %s=%s" % (abi, p) for p in sdl_libs + [so])
            n.build(apk, "apk", [dex, so] + sdl_libs, implicit=[keystore, "android/AndroidManifest.xml.in", me],
                    variables={"package": "net.srnative.sonicriders.probe", "label": "SR_Probe", "vcode": "1",
                               "vname": "m1", "dex": dex, "libs": libs})
            defaults.append(apk)
        n.default(defaults)
    print("wrote build.ninja (android probe, %s)" % ", ".join(abis))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, rest = sys.argv[1], sys.argv[2:]
    if cmd == "sdl":
        step_sdl(*rest[:4])
    elif cmd == "dex":
        step_dex(rest[0], rest[1], rest[2], rest[3], rest[4:])
    elif cmd == "keystore":
        step_keystore(rest[0], rest[1])
    elif cmd == "apk":
        import argparse
        ap = argparse.ArgumentParser()
        for k in ("jdk", "build-tools", "platform", "manifest", "package", "label", "version-code", "version-name", "dex",
                  "keystore", "out"):
            ap.add_argument("--" + k, required=True)
        ap.add_argument("--lib", action="append", default=[])
        step_apk(ap.parse_args(rest))
    else:
        sys.exit("unknown step %s" % cmd)


if __name__ == "__main__":
    main()
