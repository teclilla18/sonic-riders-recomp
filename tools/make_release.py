#!/usr/bin/env python3
"""Make the release zip: the committed source plus the setup tool, nothing else.

  python tools/make_release.py --version v0.2.0 [--flavor switch|pc|android|source] [--ref HEAD] [--out build/release]

Flavors (one download per platform, for players):
  switch  SonicRiders-Switch-Setup-<version>.zip: "Install Sonic Riders (Switch).bat" + README-FIRST.txt
  pc      SonicRiders-PC-Setup-<version>.zip: "Install Sonic Riders (PC).bat" + README-FIRST.txt
  android SonicRiders-Android-Setup-<version>.zip: "Install Sonic Riders (Android).bat" + README-FIRST.txt
  linux   SonicRiders-Linux-Setup-<version>.zip: "Install Sonic Riders (Linux).sh" (SonicRidersSetup.sh) +
          README-FIRST.txt, for Linux PCs and the Steam Deck (tools/setup_linux.py)
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
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_no_game_data as guard  # noqa: E402

CRLF_EXTS = (".bat", ".cmd", ".ps1")


def git(*args):
    return subprocess.run(("git",) + args, capture_output=True, check=True).stdout


PLAT = {"switch": ("Switch", "switch"), "pc": ("PC", "pc"), "android": ("Android", "android"), "linux": ("Linux", "linux")}


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


def readme_first(flavor, version):
    if flavor == "linux":
        return readme_linux(version)
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
            '3. Double-click "Install Sonic Riders (Switch).bat". If Windows warns, click More info > Run anyway.',
            "   Pick your disc image and your SD card. The first run takes 10-30 minutes.",
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
            '3. Double-click "Install Sonic Riders (Android).bat". If Windows warns, click More info > Run anyway.',
            "   Accept the Android SDK License when asked, pick your disc image, and say y to install.",
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
            '2. Double-click "Install Sonic Riders (PC).bat". If Windows warns, click More info > Run anyway.',
            "   Pick your disc image. The first run takes 10-30 minutes.",
            '3. Start the game with the "Sonic Riders" shortcut (in the folder, and on your desktop if you said yes).',
            "",
            "Controls: a game controller works (Xbox-style; up to 4 players with more controllers).",
            "Keyboard: WASD = move, J = A, K = B, L = X, I = Y, Enter = START, U / O = L / R, P = Z,",
            "arrow keys = C-stick, T F G H = D-pad.",
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="dev", help="version in the zip name (default: dev)")
    ap.add_argument("--ref", default="HEAD", help="commit to package (default: HEAD)")
    ap.add_argument("--out", default=os.path.join("build", "release"), help="output folder")
    ap.add_argument("--flavor", choices=["switch", "pc", "android", "linux", "source"], default="source")
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
        if args.flavor == "linux":
            setup_sh = [d for n, d in files if n == "SonicRidersSetup.sh"][0]
            files = [(n, d) for n, d in files if n not in ("SonicRidersSetup.bat", "SonicRidersSetup.sh")]
            files.append(("Install Sonic Riders (Linux).sh", setup_sh))
        else:
            files = [(n, d) for n, d in files if n not in ("SonicRidersSetup.bat", "SonicRidersSetup.sh")]
            files.append(("Install Sonic Riders (%s).bat" % plat, launcher(args.flavor)))
        files.append(("README-FIRST.txt", readme_first(args.flavor, args.version)))
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, zipname)
    tmp = path + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n, data in files:
            info = zipfile.ZipInfo("%s/%s" % (top, n), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            # shell scripts executable (unzip and file managers keep the mode)
            info.external_attr = (0o755 if n.endswith(".sh") else 0o644) << 16
            info.create_system = 3  # Unix, so the mode above is used
            z.writestr(info, data)
    os.replace(tmp, path)
    print("make_release: %s (%d files, %d bytes, commit %s)" % (path, len(files), os.path.getsize(path), commit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
