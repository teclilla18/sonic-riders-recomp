#!/usr/bin/env python3
"""Sonic Riders setup for iPhone and iPad, on a Mac (started by "Install Sonic Riders (iOS).command").

    python3 tools/setup_mac.py [--iso IMAGE] [--yes] [--tools DIR] [--install yes|no] [--device ID]
                               [--unsigned]

Builds the iOS app on this Mac from your own disc image, like the PC setups:
  1. check your disc image (Sonic Riders USA, GXEE8P revision 0, SHA-1 of the Redump dump)
  2. check Xcode (App Store; its iOS SDK and Metal Toolchain), Homebrew's ninja and shaderc, and your
     Apple ID in Xcode (optional: without one the app is built unsigned, for Sideloadly / AltStore)
  3. download the tools, each checked by SHA-256 (setup/toolchain.json "mac" + "imgui"):
     decomp-toolkit, MoltenVK, SDL2's source, Dear ImGui
  4. extract main.dol and _Main.rel from the image
  5. analyse the game code (decomp-toolkit)
  6. recompile and build (configure.py --target ios, ninja): SonicRiders.ipa, also copied to
     ~/Desktop/Sonic Riders iOS/
  7. with an Apple ID in Xcode and a device on the cable: install the app and copy the disc image to it
Nothing from the game leaves this Mac except to your own device. Apple silicon Macs (M1 or newer).
Docs: docs/notes/ios.md, INSTALL.md.
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup_linux as S  # noqa: E402  (log, step, fail, ask, run, check_iso, ensure_tool: the same steps)
from setup_linux import ROOT, log, step, fail, ask, run  # noqa: E402

S.STEPS = 7
XCODE = "/Applications/Xcode.app/Contents/Developer"
IPA_DIR = os.path.join(os.path.expanduser("~"), "Desktop", "Sonic Riders iOS")


def out(cmd, env=None):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, env=env).stdout.strip()
    except OSError:
        return ""


def pick_iso():
    """macOS's own file dialog (AppleScript), else a typed path."""
    r = subprocess.run(["osascript", "-e", 'POSIX path of (choose file with prompt "Select your Sonic Riders (USA) '
                        'disc image (GXEE8P, revision 0, .iso)")'], capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip():
        return r.stdout.strip()
    if sys.stdin.isatty():
        return input("Path of your disc image (.iso; you can drag the file into this window): ").strip().strip("'\"")
    return ""


def check_xcode():
    """Xcode itself (not only the Command Line Tools); our commands use it through DEVELOPER_DIR, so no sudo
    xcode-select is needed"""
    dev = out(["xcode-select", "-p"])
    if not dev.endswith("Xcode.app/Contents/Developer") and not dev.endswith(".app/Contents/Developer"):
        if os.path.isdir(XCODE):
            os.environ["DEVELOPER_DIR"] = XCODE
            dev = XCODE
        else:
            fail("Xcode is not installed (only the Command Line Tools are).",
                 "Install Xcode from the App Store (free, about 10 GB), open it once and let it finish setting up "
                 "(choose iOS when it asks which platforms), then run this setup again.")
    log("Xcode: %s" % (out(["xcodebuild", "-version"]).replace("\n", ", ") or dev))
    if subprocess.run(["xcodebuild", "-checkFirstLaunchStatus"], capture_output=True).returncode != 0:
        fail("Xcode has not finished its first start.",
             "Open Xcode once, accept the license and let it install its components, then run this setup again.")
    if not out(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"]):
        fail("Xcode has no iOS SDK.", "Open Xcode > Settings > Components and install iOS, then run this setup again.")
    if subprocess.run(["xcrun", "-sdk", "iphoneos", "metal", "--version"], capture_output=True).returncode != 0:
        run("download Xcode's Metal Toolchain (once)", ["xcodebuild", "-downloadComponent", "MetalToolchain"])


def check_brew_tools(assume_yes):
    """ninja and glslc (shaderc) from Homebrew; offers to install them"""
    for d in ("/opt/homebrew/bin", "/usr/local/bin"):  # a Terminal started from Finder may lack them
        if os.path.isdir(d) and d not in os.environ["PATH"].split(":"):
            os.environ["PATH"] = d + ":" + os.environ["PATH"]
    missing = [pkg for exe, pkg in (("ninja", "ninja"), ("glslc", "shaderc")) if not shutil.which(exe)]
    if not missing:
        log("ninja and glslc (Homebrew): found.")
        return
    brew = shutil.which("brew")
    if not brew:
        fail("Homebrew is needed for two small build tools (%s)." % " and ".join(missing),
             "Install Homebrew (https://brew.sh: paste its one-line command into Terminal), then run this setup again.")
    if not ask("Install %s with Homebrew now? (y/n)" % " and ".join(missing), "y", assume_yes).lower().startswith("y"):
        fail("ninja and shaderc are needed.", "Run: brew install ninja shaderc, then this setup again.")
    run("brew install " + " ".join(missing), [brew, "install"] + missing)


def find_team():
    import ios_build
    return ios_build.find_team()


def devices():
    import ios_run
    try:
        return [d for d in ios_run.devices() if "paired" in d["state"]]
    except Exception:  # devicectl missing or failing: no install step, the .ipa still works
        return []


def main():
    ap = argparse.ArgumentParser(description="Sonic Riders setup for iPhone / iPad (on a Mac)")
    ap.add_argument("--iso", default="", help="your Sonic Riders (USA) disc image (GXEE8P revision 0, 1.4 GB .iso)")
    ap.add_argument("--yes", action="store_true", help="no questions (needs --iso)")
    ap.add_argument("--tools", default=os.path.join(os.path.expanduser("~"), ".local", "share", "SonicRidersSetup", "tools-mac"),
                    help="where the downloaded tools go (default ~/.local/share/SonicRidersSetup/tools-mac)")
    ap.add_argument("--install", choices=["", "yes", "no"], default="",
                    help="install on a connected iPhone/iPad (needs your Apple ID in Xcode; default: ask)")
    ap.add_argument("--device", default="", help="which connected device (name or id) when there are several")
    ap.add_argument("--unsigned", action="store_true", help="build an unsigned .ipa even with an Apple ID in Xcode")
    ap.add_argument("--ipa-dir", default=IPA_DIR, help="where SonicRiders.ipa is copied (default ~/Desktop/Sonic Riders iOS)")
    a = ap.parse_args()
    ipa_dir = os.path.abspath(os.path.expanduser(a.ipa_dir))

    with open(S.LOG, "w", encoding="utf-8") as f:
        f.write("Sonic Riders setup (iOS, on a Mac) %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    log("Sonic Riders setup for iPhone and iPad")
    log("System: macOS %s %s, Python %s" % (platform.mac_ver()[0], platform.machine(), platform.python_version()))
    if platform.system() != "Darwin":
        fail("This setup is for a Mac. On Windows use the PC / Android setups, on Linux SonicRidersSetup.sh.")
    if platform.machine() != "arm64":
        fail("This setup needs an Apple silicon Mac (M1 or newer).")
    if not os.path.isfile(os.path.join(ROOT, "configure.py")):
        fail("This does not look like the complete release folder (configure.py is missing).",
             "Extract the whole zip first, then run the installer from inside it.")
    if " " in ROOT or not ROOT.isascii():
        fail("This folder's path has spaces or non-English letters: %s" % ROOT,
             "Move the folder to e.g. ~/SonicRiders (Finder: drag it into your home folder) and run it again.")
    import json
    tc = json.load(open(os.path.join(ROOT, "setup", "toolchain.json"), encoding="utf-8"))
    mac = tc["mac"]

    # 1 -------------------------------------------------------------
    step(1, "Your disc image")
    iso = a.iso
    if not iso:
        if a.yes:
            fail("--iso is required with --yes.")
        iso = pick_iso()
        if not iso:
            fail("No disc image selected.", "Run it again and pick the image.")
    iso = os.path.abspath(os.path.expanduser(iso))
    iso_size = S.check_iso(iso)

    # 2 -------------------------------------------------------------
    step(2, "Xcode, build tools and your Apple ID")
    check_xcode()
    check_brew_tools(a.yes)
    team = "" if a.unsigned else find_team()
    if team:
        log("Apple ID in Xcode: team %s. The app is signed for your devices (a free Apple ID: for 7 days)." % team, "green")
    else:
        log("No Apple ID in Xcode: the app is built unsigned; Sideloadly or AltStore sign it with your Apple ID.", "yellow")
        log("(To install straight from this Mac instead: Xcode > Settings > Accounts > + > Apple ID, then run again.)")

    # 3 -------------------------------------------------------------
    step(3, "Tools (downloaded once, checked by SHA-256)")
    tools = os.path.abspath(os.path.expanduser(a.tools))
    if " " in tools:
        fail("The tools folder must not contain spaces: %s" % tools, "Pass another one with --tools DIR.")
    os.makedirs(tools, exist_ok=True)
    log("Tools folder: %s" % tools)
    dtk = os.path.join(S.ensure_tool(mac["dtk"], tools), "dtk")
    mvk = S.ensure_tool(mac["moltenvk"], tools)
    sdl = S.ensure_tool(mac["sdl2_src"], tools)
    imgui = S.ensure_tool(tc["imgui"], tools)
    subprocess.run(["xattr", "-dr", "com.apple.quarantine", dtk], capture_output=True)
    log("decomp-toolkit, MoltenVK, SDL2 and Dear ImGui ready.", "green")

    # 4 -------------------------------------------------------------
    step(4, "Extracting the game code from your disc image")
    baserom = os.path.join(ROOT, "baserom")
    os.makedirs(baserom, exist_ok=True)
    run("extract main.dol and _Main.rel", [sys.executable, "-I", "tools/extract_iso.py", iso, "baserom", "--no-verify-iso"])

    # 5 -------------------------------------------------------------
    step(5, "Analysing the game code")
    run("decomp-toolkit analysis", [dtk, "dol", "split", "config/GXEE8P/config.yml", "build/GXEE8P"])

    # 6 -------------------------------------------------------------
    step(6, "Recompiling and building the app (10-20 minutes the first time)")
    conf = [sys.executable, "configure.py", "--target", "ios", "--sdl-src", sdl, "--moltenvk", mvk, "--imgui", imgui,
            "--ipa-dir", ipa_dir, "--team", team or "none"]
    run("configure", conf)
    run("build the app", ["ninja"])
    ipa = os.path.join(ipa_dir, "SonicRiders.ipa")
    if not os.path.isfile(ipa):
        fail("The build finished but %s is missing." % ipa)
    log("Built %s" % ipa, "green")

    # 7 -------------------------------------------------------------
    step(7, "Installing")
    installed = False
    devs = devices() if team else []
    if team and not devs:
        log("No iPhone/iPad found on the cable (connect it, unlock it, tap Trust; Developer Mode must be on).", "yellow")
    if devs:
        if a.device:
            devs = [d for d in devs if a.device in (d["id"], d["name"])]
        if len(devs) > 1:
            log("Connected: " + ", ".join(d["name"] for d in devs))
            want = ask("Which one? (name)", devs[0]["name"], a.yes)
            devs = [d for d in devs if d["name"] == want] or devs[:1]
        d = devs[0] if devs else None
        if d and (a.install or ("yes" if ask("Install on %s and copy your disc image to it (1.4 GB)? (y/n)" % d["name"],
                                                  "y", a.yes).lower().startswith("y") else "no")) == "yes":
            run("install the app on %s" % d["name"], [sys.executable, "tools/ios_run.py", "--device", d["id"], "install"])
            run("copy the disc image to %s (a few minutes)" % d["name"],
                [sys.executable, "tools/ios_run.py", "--device", d["id"], "push-iso", iso])
            installed = True
    log()
    if installed:
        log("Done. Sonic Riders is on your %s." % ("iPad" if "iPad" in d["model"] else "iPhone"), "green")
        log("First start: if iOS says 'Untrusted Developer', open Settings > General > VPN & Device Management,")
        log("tap your Apple ID under Developer App and tap Trust.")
        log("A free Apple ID's apps stop opening after 7 days. To renew, install the .ipa again with Sideloadly:")
        log("  %s" % ipa, "cyan")
        log("(or run this setup again with the device connected). Your saves and the disc image stay.")
    else:
        log("Done. The app: %s" % ipa, "green")
        log("Install it with Sideloadly (sideloadly.io) or AltStore with your Apple ID:")
        log("  1. Connect the iPhone/iPad, open Sideloadly, drag SonicRiders.ipa into it, enter your Apple ID, Start.")
        log("  2. On the device: Settings > General > VPN & Device Management > your Apple ID > Trust.")
        log("     (iOS 16 or newer also needs Settings > Privacy & Security > Developer Mode: on.)")
        log("  3. Copy your disc image to the app: in Finder click the device, Files tab, drag the .iso onto")
        log("     Sonic Riders, and wait until Finder has finished before opening the game.")
        log("  A free Apple ID's apps stop opening after 7 days: install this same .ipa again with Sideloadly")
        log("  to renew (your saves and the disc image stay):")
        log("  %s" % ipa, "cyan")
    log("Keep this folder: a new version is built from it again. Never share the .ipa or your disc image.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        fail("Stopped.")
