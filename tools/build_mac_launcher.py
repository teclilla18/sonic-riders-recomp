#!/usr/bin/env python3
"""Build the Mac launcher (launcher/mac/SonicRidersLauncher.swift) into "Sonic Riders Launcher.app".

    python3 tools/build_mac_launcher.py [--out DIR]     (default build/mac-launcher)

Needs a Mac with Xcode or the Command Line Tools (swiftc, iconutil, codesign). The app holds no game code or data, so
the iOS release zip carries a ready-made copy (make_release.py --flavor ios). Apple silicon only (like the iOS
build). Signed ad hoc (codesign -s -): a downloaded copy then gets macOS's "Open Anyway" in Privacy & Security
instead of "damaged" (an unsigned or half-signed bundle). Docs: docs/notes/ios.md "Mac launcher".
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
APP_NAME = "Sonic Riders Launcher.app"
MIN_MACOS = "13.0"

INFO_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key><string>SonicRidersLauncher</string>
	<key>CFBundleIdentifier</key><string>net.srnative.sonicriders.launcher</string>
	<key>CFBundleName</key><string>Sonic Riders Launcher</string>
	<key>CFBundleDisplayName</key><string>Sonic Riders Launcher</string>
	<key>CFBundleIconFile</key><string>AppIcon</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>1.0</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>LSMinimumSystemVersion</key><string>%s</string>
	<key>LSArchitecturePriority</key><array><string>arm64</string></array>
	<key>NSHighResolutionCapable</key><true/>
	<key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
""" % MIN_MACOS


def icns(out_dir, work):
    """the PC launcher's drawing (tools/launcher_icon.py: gold ring on blue) as AppIcon.icns"""
    import launcher_icon
    import make_icon
    big = os.path.join(work, "icon512.png")
    if not os.path.isfile(big):  # pure Python drawing: ~10 s
        with open(big, "wb") as f:
            f.write(make_icon.png(launcher_icon.draw(512)))
    iconset = os.path.join(work, "AppIcon.iconset")
    shutil.rmtree(iconset, ignore_errors=True)
    os.makedirs(iconset)
    for pt in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = pt * scale
            name = "icon_%dx%d%s.png" % (pt, pt, "@2x" if scale == 2 else "")
            subprocess.check_call(["sips", "-z", str(px), str(px), big, "--out", os.path.join(iconset, name)],
                                  stdout=subprocess.DEVNULL)
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", os.path.join(out_dir, "AppIcon.icns")])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "mac-launcher"))
    a = ap.parse_args()
    work = os.path.abspath(a.out)
    app = os.path.join(work, APP_NAME)
    shutil.rmtree(app, ignore_errors=True)
    macos, res = os.path.join(app, "Contents", "MacOS"), os.path.join(app, "Contents", "Resources")
    os.makedirs(macos)
    os.makedirs(res)
    exe = os.path.join(macos, "SonicRidersLauncher")
    subprocess.check_call(["xcrun", "swiftc", "-O", "-swift-version", "5", "-parse-as-library",
                           "-target", "arm64-apple-macos" + MIN_MACOS,
                           os.path.join(ROOT, "launcher", "mac", "SonicRidersLauncher.swift"), "-o", exe])
    with open(os.path.join(app, "Contents", "Info.plist"), "w", encoding="utf-8") as f:
        f.write(INFO_PLIST)
    banner = os.path.join(ROOT, "launcher", "banner.jpg")
    if os.path.isfile(banner):
        shutil.copyfile(banner, os.path.join(res, "banner.jpg"))
    icns(res, work)
    # seal the whole bundle (binary, Info.plist, resources): a bundle whose files don't match its signature is
    # "damaged" to Gatekeeper, with no Open Anyway
    subprocess.check_call(["codesign", "--force", "--deep", "--sign", "-", app])
    subprocess.check_call(["codesign", "--verify", "--deep", "--strict", app])
    print("build_mac_launcher: %s" % os.path.relpath(app, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
