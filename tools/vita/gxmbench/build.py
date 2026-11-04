"""Build probe V1b (the GXM microbench) as build/vita/gxmbench/gxmbench.vpk, title SRNP00002.
usage: python tools/vita/gxmbench/build.py [--vitasdk DIR]   (default: $VITASDK)
The app needs the user's libshacccg at ur0:data/libshacccg.suprx (ShaRKF00D) and writes
ux0:data/gxmbench/log.txt."""
import argparse, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
ap = argparse.ArgumentParser()
ap.add_argument("--vitasdk", default=os.environ.get("VITASDK", ""))
a = ap.parse_args()
sdk = a.vitasdk.replace("\\", "/")
if not os.path.isdir(os.path.join(sdk, "bin")):
    sys.exit("VitaSDK not found (--vitasdk DIR or $VITASDK)")
out = "build/vita/gxmbench"  # relative: the VitaSDK tools fail on paths with spaces
os.makedirs(os.path.join(ROOT, out), exist_ok=True)
b = lambda t: os.path.join(sdk, "bin", t)


def run(*cmd):
    print(" ".join(cmd))
    # vita-elf-create crashed (0xC0000374) on this program linked without --gc-sections
    subprocess.check_call(list(cmd), cwd=ROOT)


elf = out + "/" + "gxmbench.elf"
run(b("arm-vita-eabi-gcc"), "-O2", "-std=gnu11", "-Wall", "-march=armv7-a", "-mtune=cortex-a9", "-mfpu=neon",
    "-mfloat-abi=hard", "-mthumb", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Wl,-q", "tools/vita/gxmbench/gxmbench.c", "-o", elf,
    "-lSceGxm_stub", "-lSceDisplay_stub", "-lSceShaccCg_stub_weak", "-lScePower_stub", "-lSceKernelModulemgr_stub",
    "-lSceSysmem_stub", "-lm")
run(b("vita-elf-create"), elf, out + "/" + "gxmbench.velf")
run(b("vita-make-fself"), "-c", out + "/" + "gxmbench.velf", out + "/" + "eboot.bin")
run(b("vita-mksfoex"), "-s", "TITLE_ID=SRNP00002", "Sonic Riders probe V1b", out + "/" + "param.sfo")
run(b("vita-pack-vpk"), "-s", out + "/" + "param.sfo", "-b", out + "/" + "eboot.bin",
    out + "/" + "gxmbench.vpk")
print("built", out + "/" + "gxmbench.vpk")
