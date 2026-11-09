#!/usr/bin/env python3
"""Make the release zip: the committed source plus the setup tool, nothing else.

  python tools/make_release.py --version v0.2.0 [--flavor switch|pc|android|source] [--ref HEAD] [--out build/release]

Flavors (one download per platform, for players):
  switch  SonicRiders-Switch-Setup-<version>.zip: "Install Sonic Riders (Switch).exe" (the launcher in installer
          mode) + "Install Sonic Riders (Switch) - command line.bat" + README-FIRST.txt
  pc      SonicRiders-PC-Setup-<version>.zip: SonicRidersLauncher.exe (built here by tools/build_launcher.py
          from the committed launcher source) + "Install Sonic Riders (PC).bat" + README-FIRST.txt
  android SonicRiders-Android-Setup-<version>.zip: "Install Sonic Riders (Android).exe" (installer mode) +
          "Install Sonic Riders (Android) - command line.bat" + README-FIRST.txt
  linux   SonicRiders-Linux-Setup-<version>.zip: "Install Sonic Riders (Linux).sh" (SonicRidersSetup.sh) +
          README-FIRST.txt, for Linux PCs and the Steam Deck (tools/setup_linux.py)
  ios     SonicRiders-iOS-Setup-<version>.zip: "Sonic Riders Launcher.app" (built here, on a Mac, by
          tools/build_mac_launcher.py from the committed source) + "START HERE (Mac).html" + "Install Sonic
          Riders (iOS).command" (Terminal fallback) + README-FIRST.txt, for iPhone / iPad, built on the player's
          Mac (tools/setup_mac.py; no .ipa can be published)
  source  sonic-riders-recomp-<version>-<commit>.zip: the tree as is (SonicRidersSetup.bat asks)

The zip holds every file tracked in git at REF (default HEAD) under one top folder, plus a VERSION
file (version and commit, logged by the port at startup). Each file goes
through the same check as the pre-commit hook and CI (tools/check_no_game_data.py); if any file is
refused, no zip is written. Windows scripts (.bat, .ps1, .cmd) get CRLF line endings.
Uncommitted changes are not included; the script warns when the working tree differs from REF.
"""
import argparse
import os
import subprocess
import sys
import time
import stat
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_no_game_data as guard  # noqa: E402

CRLF_EXTS = (".bat", ".cmd", ".ps1")


def git(*args):
    return subprocess.run(("git",) + args, capture_output=True, check=True).stdout


PLAT = {"switch": ("Switch", "switch"), "pc": ("PC", "pc"), "android": ("Android", "android"), "linux": ("Linux", "linux"),
        "ios": ("iOS", "ios")}


def launcher(flavor):
    plat, target = PLAT[flavor]
    lines = [
        "@echo off",
        "rem Sonic Riders native port: builds the %s version from your own disc image (see README-FIRST.txt)" % plat,
        'powershell -NoProfile -ExecutionPolicy Bypass -File "%%~dp0setup\\setup.ps1" -Target %s %%*' % target,
        # keep setup's exit code for unattended runs (tools/sandbox_test.py sets SR_NOPAUSE=1)
        "set SR_SETUP_RC=%ERRORLEVEL%",
        "echo.",
        "if not defined SR_NOPAUSE pause",
        "exit /b %SR_SETUP_RC%",
    ]
    return ("\r\n".join(lines) + "\r\n").encode()


IOS_COMMAND = """#!/bin/sh
# Sonic Riders native port: builds the iPhone / iPad app on this Mac from your disc image (README-FIRST.txt)
cd "$(dirname "$0")" || exit 1
if [ ! -d /Applications/Xcode.app ]; then
  echo "Xcode is needed: install it from the App Store (free), open it once, then run this again."
  printf "Press Return to close. "; read -r _; exit 1
fi
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer /usr/bin/python3 tools/setup_mac.py "$@"
rc=$?
echo
[ -n "$SR_NOPAUSE" ] || { printf "Press Return to close. "; read -r _; }
exit $rc
"""


def readme_first(flavor, version):
    if flavor == "linux":
        return readme_linux(version)
    if flavor == "ios":
        return readme_ios(version)
    common = [
        "Sonic Riders native port %s - unofficial fan project, not affiliated with SEGA or Nintendo." % version,
        "",
        "This download contains NO game files. You need your own Sonic Riders (USA) disc image:",
        "GXEE8P, revision 0, a clean full .iso (1,459,978,240 bytes). The setup checks it.",
        "",
        "The setup runs on a 64-bit Windows 10 (version 1803 or newer) or Windows 11 PC, with about 4 GB",
        "free and internet the first time. Nothing else to install: it downloads its own build tools.",
        "",
    ]
    if flavor == "switch":
        body = [
            "PLAY ON A NINTENDO SWITCH (custom firmware + Homebrew Menu; 30 fps at stock clocks, overclock for 60)",
            "",
            "1. Extract this whole zip to a folder (for example C:\\SonicRiders). Its path must use only",
            "   English letters and digits (not your Downloads folder if your Windows user name has accents).",
            "2. Put your Switch's SD card in the PC.",
            '3. Double-click "Install Sonic Riders (Switch).exe". If Windows warns, click More info > Run anyway.',
            "   In its window pick your disc image and your SD card (and, if you like, your own icon or a",
            "   HOME-screen icon), then press BUILD AND INSTALL. The first run takes 10-30 minutes.",
            '   (The same setup in a command window: "Install Sonic Riders (Switch) - command line.bat".)',
            "4. Eject the SD card, put it in the Switch, hold R while starting any game to open the",
            "   Homebrew Menu, and start Sonic Riders.",
            "",
            "In game: hold Minus for the settings menu (widescreen, FPS counter, controllers, screenshots).",
        ]
    elif flavor == "android":
        body = [
            "PLAY ON AN ANDROID PHONE (64-bit, Android 11 or newer, Vulkan; tested on a Galaxy S23 Ultra)",
            "",
            "The game is built on this PC from your disc image and installed on the phone over a USB cable.",
            "",
            "On the phone, once: turn on USB debugging.",
            "  Settings > About phone > Software information > tap Build number 7 times (Developer options),",
            "  then Settings > Developer options > USB debugging: on.",
            "",
            "1. Extract this whole zip to a folder you'll keep (for example C:\\SonicRiders). Its path must use",
            "   only English letters and digits. Install updates from this same PC: it keeps the signing key.",
            "2. Connect the phone with a USB cable, unlock it, and choose File transfer if it asks.",
            '3. Double-click "Install Sonic Riders (Android).exe". If Windows warns, click More info > Run anyway.',
            "   In its window pick your disc image, accept the Android SDK License and press BUILD AND INSTALL.",
            '   (The same setup in a command window: "Install Sonic Riders (Android) - command line.bat".)',
            '   When the phone asks "Allow USB debugging?", tap Allow.',
            "   The first run downloads about 1.1 GB of Android build tools (about 8 GB free needed on the PC)",
            "   and takes 20-40 minutes.",
            "4. The setup installs the game and copies your disc image (1.4 GB) to the phone.",
            "   Start Sonic Riders from the phone's app list.",
            "",
            "Controls: touch controls on screen, or any Bluetooth/USB controller. Top-left button = settings menu.",
            "Your saves live inside the app: uninstalling the app deletes them.",
            "",
            "Known issue: the game can crash while you record the screen (and rarely when the phone is very warm).",
        ]
    else:
        body = [
            "PLAY ON WINDOWS (no emulator: the game runs natively)",
            "",
            "Needs: a graphics card with OpenGL 3.3.",
            "",
            "1. Extract this whole zip to a folder you'll keep (for example C:\\Games\\SonicRiders). Its path",
            "   must use only English letters and digits (not under a Windows user name with accents).",
            "   The game, your saves and settings live in that folder.",
            "2. Double-click SonicRidersLauncher.exe. If Windows warns, click More info > Run anyway.",
            '   Click "Select disc...", pick your disc image and press INSTALL AND PLAY. The first run takes',
            "   10-30 minutes; after that the launcher's PLAY button starts the game.",
            "   The launcher also has the settings, mods and button remapping (Controls page).",
            '   (Without the launcher: double-click "Install Sonic Riders (PC).bat", then use the "Sonic Riders"',
            "   shortcut it makes.)",
            "",
            "Controls: a game controller works (Xbox-style; up to 4 players with more controllers).",
            "Keyboard: WASD = move, J = A, K = B, L = X, I = Y, Enter = START, U / O = L / R, P = Z,",
            "arrow keys = C-stick, T F G H = D-pad. Change them on the launcher's Controls page.",
            "Hold F1 (or the controller's Back/View button) = settings menu. F11 or Alt+Enter = fullscreen.",
        ]
    tail = [
        "",
        "Updating: download the new version, extract it to a new folder and run the installer again.",
        "Problems or bugs: https://github.com/teclilla18/sonic-riders-recomp/issues (attach setup.log or",
        "sonicriders.log; never attach game files).",
        "",
        "Never share your disc image or the files the setup builds from it.",
    ]
    return ("\r\n".join(common + body + tail) + "\r\n").encode()


def readme_linux(version):
    lines = [
        "Sonic Riders native port %s - unofficial fan project, not affiliated with SEGA or Nintendo." % version,
        "",
        "This download contains NO game files. You need your own Sonic Riders (USA) disc image:",
        "GXEE8P, revision 0, a clean full .iso (1,459,978,240 bytes). The setup checks it.",
        "",
        "PLAY ON LINUX OR THE STEAM DECK (no emulator: the game runs natively)",
        "",
        "Needs: a 64-bit Linux with Python 3, SDL2 and OpenGL 3.3 (SteamOS has all three), about 4 GB",
        "free and internet the first time. Nothing is installed into the system and no root rights are",
        "needed: the setup downloads its own build tools (and a compiler if the system has none).",
        "",
        "1. Extract this whole zip to a folder you'll keep (for example ~/Games/SonicRiders). On the Steam",
        "   Deck do this in Desktop Mode. The game, your saves and settings live in that folder.",
        '2. Double-click "Install Sonic Riders (Linux).sh" (choose Execute / Run if asked), or in a terminal:',
        "     cd ~/Games/SonicRiders && sh 'Install Sonic Riders (Linux).sh'",
        "   Pick your disc image. The first run takes 10-30 minutes.",
        '3. Start the game with "Sonic Riders" in the application menu (Games), or ./"Sonic Riders.sh".',
        "",
        "Steam Deck Game Mode: in Desktop Mode open Steam > Games > Add a Non-Steam Game to My Library, tick",
        '"Sonic Riders" and click Add. It then starts from Game Mode with the Deck\'s controls.',
        "",
        "Controls: a game controller works (Xbox-style; up to 4 players with more controllers).",
        "Keyboard: WASD = move, J = A, K = B, L = X, I = Y, Enter = START, U / O = L / R, P = Z,",
        "arrow keys = C-stick, T F G H = D-pad.",
        "Hold F1 (or the controller's Back/View button) = settings menu. F11 or Alt+Enter = fullscreen.",
        "",
        "Updating: download the new version, extract it to a new folder and run the installer again.",
        "Problems or bugs: https://github.com/teclilla18/sonic-riders-recomp/issues (attach setup.log or",
        "sonicriders.log from the game folder; never attach game files).",
        "",
        "Never share your disc image or the files the setup builds from it.",
    ]
    return ("\n".join(lines) + "\n").encode()


def readme_ios(version):
    lines = [
        "Sonic Riders native port %s - unofficial fan project, not affiliated with SEGA or Nintendo." % version,
        "",
        "This download contains NO game files. You need your own Sonic Riders (USA) disc image:",
        "GXEE8P, revision 0, a clean full .iso (1,459,978,240 bytes). The setup checks it.",
        "",
        "PLAY ON IPHONE OR IPAD (iOS / iPadOS 16 or newer; no emulator: the game runs natively)",
        "",
        "The app is built on your Mac from your disc image (nobody may share a ready-made app, it would",
        "contain the game). Tested: races at 60 fps on iPhone 17 Pro, iPad Pro 11-inch (M1) and iPad (A16);",
        "iPad 7th generation (2019) plays with small dips on busy tracks.",
        "",
        "Needs: an Apple silicon Mac (M1 or newer) with Xcode (App Store, free, about 10 GB; open it once",
        "and add iOS), Homebrew (brew.sh; the setup installs two small tools with it), about 6 GB free and",
        "internet the first time. A free Apple ID is enough.",
        "",
        "1. Double-click the zip and keep the folder it makes (for example in your home folder).",
        '2. Open "START HERE (Mac).html": the first start of "Sonic Riders Launcher" needs a few clicks in',
        "   macOS, once (the launcher is free and not signed by Apple):",
        '     a. double-click "Sonic Riders Launcher";',
        '     b. a warning appears: click Done, the lower grey button (NOT the blue "Move to Trash" above it);',
        "     c. System Settings > Privacy & Security, scroll down to Security: Open Anyway;",
        '     d. "Open Sonic Riders Launcher.app?": click Open Anyway, the MIDDLE button (not the blue',
        '        "Move to Trash" on top), then Touch ID or Use Password...;',
        "     e. the launcher asks for your Downloads and Desktop folders: Allow both times.",
        "3. The launcher guides you through the rest: Get ready (Xcode, Metal Toolchain, Homebrew tools, each",
        "   with a button), Build (your disc image, optional app icon from any picture; 15-30 minutes the",
        "   first time), Install (Sideloadly step by step) and Renew (every 7 days).",
        '   Without the launcher: double-click "Install Sonic Riders (iOS).command" (same Privacy & Security',
        "   step) or in Terminal: sh, a space, drag the .command file into the window, Return.",
        "4a. With your Apple ID in Xcode: the setup installs the app and copies your disc image to the device.",
        "    First start: Settings > General > VPN & Device Management > your Apple ID > Trust.",
        "4b. Without: the app is in ~/Desktop/Sonic Riders iOS/SonicRiders.ipa. Install it with Sideloadly",
        "    (sideloadly.io) or AltStore and your Apple ID, then copy your disc image into the app: Finder >",
        "    your device > Files tab > drag the .iso onto Sonic Riders; wait until the copy has finished",
        "    before opening the game. (Finder says \"The required file cannot be found\"? Click another place",
        "    in Finder's sidebar and then the device again, and drag it again.) Trust the developer as in 4a.",
        "A free Apple ID allows 3 sideloaded apps per device; remove one if the install says the maximum is reached.",
        "",
        "RENEWING EVERY 7 DAYS (free Apple ID): iOS stops opening the app 7 days after it was installed.",
        "The app file is always here on your Mac (the build before it: SonicRiders-prev.ipa, next to it):",
        "    ~/Desktop/Sonic Riders iOS/SonicRiders.ipa",
        "and \"How to renew (every 7 days).txt\" in that folder has the steps with your app's ID. Renew the SAME",
        "way you installed, or the device gets a second, empty Sonic Riders:",
        "  - Installed from the Mac: run the installer again with the device connected (y to installing, n to",
        "    copying the disc image), or in Sideloadly untick Advanced Options > \"Use automatic bundle ID\".",
        "  - Installed with Sideloadly: drag the same .ipa into Sideloadly with the same Apple ID and settings",
        "    (\"Use automatic bundle ID\" left ticked, the default).",
        "Your saves and the disc image stay on the device.",
        "",
        "Controls: touch (on-screen stick and buttons), or a game controller (Xbox, PlayStation, MFi).",
        "The round button at the top left opens the settings menu.",
        "",
        "Updating: download the new version, extract it to a new folder and run the installer again.",
        "Problems or bugs: https://github.com/teclilla18/sonic-riders-recomp/issues (attach setup.log, or",
        "sonicriders.log from the app's folder (Files app > On My iPhone > Sonic Riders); never attach",
        "game files).",
        "",
        "Never share your disc image, the .ipa or the files the setup builds from it.",
    ]
    return ("\n".join(lines) + "\n").encode()


LAUNCHER_SRC = ["launcher", "tools/build_launcher.py", "tools/launcher_icon.py", "tools/make_icon.py"]


def build_launcher(ref):
    """the launcher exe, built now from the working tree, which must match REF for the launcher's files"""
    if git("diff", "--name-only", ref, "--", *LAUNCHER_SRC).strip():
        print("make_release: the launcher's files differ from %s (commit them first); no zip written." % ref)
        return None
    if subprocess.run([sys.executable, os.path.join("tools", "build_launcher.py")]).returncode:
        print("make_release: the launcher did not build; no zip written.")
        return None
    with open(os.path.join("build", "launcher", "SonicRidersLauncher.exe"), "rb") as f:
        return f.read()


MAC_LAUNCHER_SRC = ["launcher/mac", "launcher/banner.jpg", "launcher/icon_256.png", "launcher/icon_32.png",
                    "tools/build_mac_launcher.py", "tools/launcher_icon.py", "tools/make_icon.py"]
MAC_APP = "Sonic Riders Launcher.app"


def build_mac_launcher(ref):
    """the Mac launcher app, built now (on a Mac) from the working tree, which must match REF for its files;
    returns [(path inside the zip, bytes, mode)]"""
    if git("diff", "--name-only", ref, "--", *MAC_LAUNCHER_SRC).strip():
        print("make_release: the Mac launcher's files differ from %s (commit them first); no zip written." % ref)
        return None
    out = os.path.join("build", "mac-launcher")
    if subprocess.run([sys.executable, os.path.join("tools", "build_mac_launcher.py"), "--out", out]).returncode:
        print("make_release: the Mac launcher did not build (needs a Mac with Xcode); no zip written.")
        return None
    files, app = [], os.path.join(out, MAC_APP)
    for d, _, names in os.walk(app):
        for n in sorted(names):
            p = os.path.join(d, n)
            if os.path.islink(p):
                print("make_release: unexpected link in the launcher app: %s" % p)
                return None
            rel = os.path.relpath(p, out).replace(os.sep, "/")
            with open(p, "rb") as f:
                files.append((rel, f.read(), 0o755 if os.access(p, os.X_OK) else 0o644))
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="dev", help="version in the zip name (default: dev)")
    ap.add_argument("--ref", default="HEAD", help="commit to package (default: HEAD)")
    ap.add_argument("--out", default=os.path.join("build", "release"), help="output folder")
    ap.add_argument("--flavor", choices=["switch", "pc", "android", "linux", "ios", "source"], default="source")
    args = ap.parse_args()

    root = git("rev-parse", "--show-toplevel").decode().strip()
    os.chdir(root)
    commit = git("rev-parse", "--short=8", args.ref).decode().strip()
    if git("status", "--porcelain", "--untracked-files=no").strip():
        print("note: the working tree has uncommitted changes; only %s (%s) is packaged" % (args.ref, commit))

    names = [n for n in git("ls-tree", "-r", "-z", "--name-only", args.ref).decode().split("\0") if n]
    files, bad = [], 0
    for n in names:
        data = git("show", "%s:%s" % (args.ref, n))
        if n != "tools/check_no_game_data.py":
            for p in guard.check(n, data):
                print("refused: %s: %s" % (n, p))
                bad += 1
        if n.lower().endswith(CRLF_EXTS):
            data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        files.append((n, data))
    if bad:
        print("make_release: %d problem(s); no zip written." % bad)
        return 1

    # the build id the port logs at startup (configure.py prefers it to git)
    files.append(("VERSION", ("%s (%s)\n" % (args.version, commit)).encode()))
    if args.flavor == "source":
        top = "sonic-riders-recomp-%s" % args.version
        zipname = "%s-%s.zip" % (top, commit)
    else:
        plat = PLAT[args.flavor][0]
        top = "SonicRiders-%s-%s" % (plat, args.version)
        zipname = "SonicRiders-%s-Setup-%s.zip" % (plat, args.version)
        if args.flavor == "ios":
            app = build_mac_launcher(args.ref)
            if app is None:
                return 1
            start = [d for n, d in files if n == "launcher/mac/START_HERE.html"][0]
            files = [(n, d) for n, d in files if n not in ("SonicRidersSetup.bat", "SonicRidersSetup.sh")]
            files.append(("Install Sonic Riders (iOS).command", IOS_COMMAND.encode()))
            files.append(("START HERE (Mac).html", start))
            files += app
        elif args.flavor == "linux":
            setup_sh = [d for n, d in files if n == "SonicRidersSetup.sh"][0]
            files = [(n, d) for n, d in files if n not in ("SonicRidersSetup.bat", "SonicRidersSetup.sh")]
            files.append(("Install Sonic Riders (Linux).sh", setup_sh))
        elif args.flavor == "pc":
            files = [(n, d) for n, d in files if n not in ("SonicRidersSetup.bat", "SonicRidersSetup.sh")]
            files.append(("Install Sonic Riders (%s).bat" % plat, launcher(args.flavor)))
        else:  # Switch, Android: the launcher in installer mode (chosen by its name) + the console setup as a fallback
            files = [(n, d) for n, d in files if n not in ("SonicRidersSetup.bat", "SonicRidersSetup.sh")]
            files.append(("Install Sonic Riders (%s) - command line.bat" % plat, launcher(args.flavor)))
        if args.flavor in ("pc", "switch", "android"):
            exe = build_launcher(args.ref)
            if not exe:
                return 1
            files.append(("SonicRidersLauncher.exe" if args.flavor == "pc" else "Install Sonic Riders (%s).exe" % plat, exe))
        files.append(("README-FIRST.txt", readme_first(args.flavor, args.version)))
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, zipname)
    tmp = path + ".part"
    ct = int(git("log", "-1", "--format=%ct", args.ref).decode().strip())
    # minus one day: zip times have no time zone and unzip tools read them as local time, so the bare UTC time would
    # lie hours in the future west of UTC (ninja then rebuilds until the clock passes it)
    stamp = time.gmtime(ct - 86400)[:6]
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for entry in files:
            n, data = entry[0], entry[1]
            # shell scripts and the launcher app's program executable (unzip, Archive Utility and file managers keep
            # the mode); S_IFREG marks a regular file, as Info-ZIP and macOS's own zip write it
            mode = entry[2] if len(entry) > 2 else 0o755 if n.endswith((".sh", ".command")) else 0o644
            # the commit's time, not a fixed date: the launcher's updater (robocopy) skips files whose size and
            # time match, so with one date for every release a same-size change (VERSION!) was never copied
            info = zipfile.ZipInfo("%s/%s" % (top, n), date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.create_system = 3  # Unix, so the mode above is used
            z.writestr(info, data)
    os.replace(tmp, path)
    print("make_release: %s (%d files, %d bytes, commit %s)" % (path, len(files), os.path.getsize(path), commit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
