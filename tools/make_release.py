#!/usr/bin/env python3
"""Make the release zip: the committed source plus the setup tool, nothing else.

  python tools/make_release.py --version v0.2.0 [--flavor switch|pc|source] [--ref HEAD] [--out build/release]

Flavors (one download per platform, for players):
  switch  SonicRiders-Switch-Setup-<version>.zip: "Install Sonic Riders (Switch).bat" + README-FIRST.txt
  pc      SonicRiders-PC-Setup-<version>.zip: "Install Sonic Riders (PC).bat" + README-FIRST.txt
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


def launcher(flavor):
    plat, target = ("Switch", "switch") if flavor == "switch" else ("PC", "pc")
    lines = [
        "@echo off",
        "rem Sonic Riders native port: builds the %s version from your own disc image (see README-FIRST.txt)" % plat,
        'powershell -NoProfile -ExecutionPolicy Bypass -File "%%~dp0setup\\setup.ps1" -Target %s %%*' % target,
        "echo.",
        "pause",
    ]
    return ("\r\n".join(lines) + "\r\n").encode()


def readme_first(flavor, version):
    common = [
        "Sonic Riders native port %s - unofficial fan project, not affiliated with SEGA or Nintendo." % version,
        "",
        "This download contains NO game files. You need your own Sonic Riders (USA) disc image:",
        "GXEE8P, revision 0, a clean full .iso (1,459,978,240 bytes). The setup checks it.",
        "",
    ]
    if flavor == "switch":
        body = [
            "PLAY ON A NINTENDO SWITCH (custom firmware + Homebrew Menu; an overclock is needed for now)",
            "",
            "1. Extract this whole zip to a folder (for example C:\\SonicRiders).",
            "2. Put your Switch's SD card in the PC.",
            '3. Double-click "Install Sonic Riders (Switch).bat". If Windows warns, click More info > Run anyway.',
            "   Pick your disc image and your SD card. The first run takes 10-30 minutes.",
            "4. Eject the SD card, put it in the Switch, hold R while starting any game to open the",
            "   Homebrew Menu, and start Sonic Riders.",
            "",
            "In game: hold Minus for the settings menu (widescreen, FPS counter, controllers, screenshots).",
        ]
    else:
        body = [
            "PLAY ON WINDOWS (no emulator: the game runs natively)",
            "",
            "Needs: 64-bit Windows 10 (1803+) or 11, a graphics card with OpenGL 3.3, about 4 GB free,",
            "and an internet connection the first time (the build tools are downloaded once).",
            "",
            "1. Extract this whole zip to a folder you'll keep (for example C:\\Games\\SonicRiders).",
            "   The game, your saves and settings live in that folder.",
            '2. Double-click "Install Sonic Riders (PC).bat". If Windows warns, click More info > Run anyway.',
            "   Pick your disc image. The first run takes 10-30 minutes.",
            '3. Start the game with the "Sonic Riders" shortcut (in the folder, and on your desktop if you said yes).',
            "",
            "Controls: a game controller works (Xbox-style; up to 4 players with more controllers).",
            "Keyboard: WASD = move, J = A, K = B, L = X, I = Y, Enter = START, U / O = L / R, P = Z,",
            "arrow keys = C-stick, T F G H = D-pad.",
            "F1 (or the controller's Back/View button) = settings menu. F11 or Alt+Enter = fullscreen.",
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="dev", help="version in the zip name (default: dev)")
    ap.add_argument("--ref", default="HEAD", help="commit to package (default: HEAD)")
    ap.add_argument("--out", default=os.path.join("build", "release"), help="output folder")
    ap.add_argument("--flavor", choices=["switch", "pc", "source"], default="source")
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
        plat = "Switch" if args.flavor == "switch" else "PC"
        top = "SonicRiders-%s-%s" % (plat, args.version)
        zipname = "SonicRiders-%s-Setup-%s.zip" % (plat, args.version)
        files = [(n, d) for n, d in files if n != "SonicRidersSetup.bat"]
        files.append(("Install Sonic Riders (%s).bat" % plat, launcher(args.flavor)))
        files.append(("README-FIRST.txt", readme_first(args.flavor, args.version)))
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, zipname)
    tmp = path + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n, data in files:
            info = zipfile.ZipInfo("%s/%s" % (top, n), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    os.replace(tmp, path)
    print("make_release: %s (%d files, %d bytes, commit %s)" % (path, len(files), os.path.getsize(path), commit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
