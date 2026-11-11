"""iOS build pieces for configure.py (--target ios), on a Mac with Xcode. docs/notes/ios.md.

ninja compiles the runtime and the recompiled code with Xcode's clang into build/ios/libsonicriders.a, builds SDL2's
static library from its source with SDL's own Xcode project, and then runs xcodebuild on a small generated project
(build/ios/xcode/) that only links, bundles and signs: Xcode makes the provisioning profile a free Apple account
needs (Personal Team, 7-day builds). Nothing here is game data; the .app it makes is (never publish it).
"""
import glob
import os
import re
import shutil
import subprocess
import struct
import sys
import zlib

MIN_IOS = "16.0"
SDK = "iphoneos"
FRAMEWORKS = ["UIKit", "Foundation", "QuartzCore", "Metal", "IOSurface", "CoreGraphics", "CoreFoundation",
              "AVFoundation", "AudioToolbox", "CoreAudio", "CoreMotion", "CoreHaptics", "GameController",
              "CoreBluetooth", "IOKit",
              "OpenGLES"]  # SDL2's UIKit video code refers to EAGL even when we render with Vulkan


def _run(cmd):
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


BASE_ID = "net.srnative.sonicriders"


def ipa_bundle_id(bundle, team):
    """the ID written into the .ipa for Sideloadly/AltStore: Sideloadly's default always appends ".<team ID>"
    (tested 2026-11-09: an .ipa with <base>.<team> became <base>.<team>.<team>), so an app installed as
    <base>.<team> gets an .ipa with <base>; any other ID stays as it is (Sideloadly: untick "automatic")"""
    return bundle[:-len(team) - 1] if team and bundle.endswith("." + team) else bundle


def find_team():
    """the Team ID of the Mac's "Apple Development" certificate (Xcode makes it when an Apple ID is added under
    Settings > Accounts); "" if there is none"""
    pem = _run(["security", "find-certificate", "-a", "-c", "Apple Development", "-p"])
    for block in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S):
        p = subprocess.run(["openssl", "x509", "-noout", "-subject", "-nameopt", "multiline"],
                           input=block, capture_output=True, text=True)
        m = re.search(r"organizationalUnitName\s*=\s*([A-Z0-9]{10})", p.stdout)
        if m:
            return m.group(1)
    # no certificate yet (a free account gets one on the first signed build): the team of an Apple ID added
    # under Xcode > Settings > Accounts; xcodebuild -allowProvisioningUpdates then makes the certificate
    m = re.search(r"teamID\s*=\s*\"?([A-Z0-9]{10})", _run(["defaults", "read", "com.apple.dt.Xcode",
                                                            "IDEProvisioningTeamByIdentifier"]))
    return m.group(1) if m else ""


class Tools:
    def __init__(self, args, root):
        self.root = root
        ext = os.path.join(root, "build", "ext")
        self.sdl_src = args.sdl_src or next(iter(sorted(glob.glob(os.path.join(ext, "SDL2-2.*")))), "")
        if not os.path.isfile(os.path.join(self.sdl_src, "Xcode", "SDL", "SDL.xcodeproj", "project.pbxproj")):
            sys.exit("SDL2 source not found (--sdl-src DIR: SDL2-2.32.10.tar.gz extracted, e.g. into build/ext)")
        self.mvk = args.moltenvk or os.path.join(ext, "MoltenVK", "MoltenVK")
        self.mvk_lib = os.path.join(self.mvk, "static", "MoltenVK.xcframework", "ios-arm64", "libMoltenVK.a")
        if not os.path.isfile(self.mvk_lib):
            sys.exit("MoltenVK not found (--moltenvk DIR: MoltenVK-ios.tar v1.4.2 extracted, the folder holding "
                     "include/ and static/)")
        self.glslc = shutil.which("glslc") or ""
        if not self.glslc:
            sys.exit("glslc not found: brew install shaderc")
        if not _run(["xcrun", "--sdk", SDK, "--show-sdk-path"]):
            sys.exit("the iOS SDK was not found: install Xcode and run xcode-select -s /Applications/Xcode.app")
        # the app icon: your own picture (--icon, any common format; kept out of git), else the default icon
        # (setup/icon.png, cedry2kio's fan art, as on PC, Android, Linux and Switch), else none
        fan = os.path.join(root, "setup", "icon.png")
        default = fan if os.path.isfile(fan) else ""
        self.icon = (os.path.abspath(args.icon) if args.icon.lower().endswith(ICON_EXTS) else
                     default if args.icon.lower() in ("", "default") else "")
        self.ipa_dir = os.path.abspath(os.path.expanduser(args.ipa_dir)) if args.ipa_dir not in ("", "none") else ""
        self.team = "" if args.team == "none" else (args.team or find_team())
        print("ios: %s" % ("signed for Apple team %s" % self.team if self.team else
                           "no Apple team: an unsigned .ipa (Sideloadly / AltStore sign it with your Apple ID)"))
        # the bundle ID must be unique across Apple: <base>.<team ID>, the same ID Sideloadly's default
        # ("Use automatic bundle ID") makes from the plain-ID .ipa, so both ways of installing reach one app
        self.bundle_id = args.bundle_id or (BASE_ID + "." + self.team if self.team else BASE_ID)
        self.ipa_id = ipa_bundle_id(self.bundle_id, self.team)

    def rel(self, p):
        return os.path.relpath(p, self.root).replace("\\", "/")

    def cc(self, cxx=False):
        return "xcrun -sdk %s clang%s -target arm64-apple-ios%s" % (SDK, "++" if cxx else "", MIN_IOS)

    def inc(self):
        return "-Iruntime/src/platform/apple -I%s/include -I%s/include -DSR_IOS=1" % (self.rel(self.sdl_src), self.rel(self.mvk))

    def sdl_lib(self):
        return "build/ios/sdl/Release-iphoneos/libSDL2.a"


def add_build(n, t, objs, out, py):
    """the static library, SDL2, the Xcode project and the signed .app; returns the .app's program path"""
    # SDL's main() for UIKit (it starts SDL's app delegate, which calls our SDL_main on the main thread)
    main_o = out + "/obj/sdl_uikit_main.o"
    n.build(main_o, "cc", t.rel(os.path.join(t.sdl_src, "src", "main", "uikit", "SDL_uikit_main.c")),
            variables={"extra": "-O2 -w"})
    objs = objs + [main_o]
    lib = out + "/libsonicriders.a"
    n.rule("lib", "rm -f $out && libtool -static -no_warning_for_no_symbols -o $out -filelist $out.rsp",
           rspfile="$out.rsp", rspfile_content="$in_newline", description="LIBTOOL $out")
    n.build(lib, "lib", objs)
    sdl_proj = t.rel(os.path.join(t.sdl_src, "Xcode", "SDL", "SDL.xcodeproj"))
    n.rule("sdl_ios", "xcodebuild -quiet -project %s -target \"Static Library-iOS\" -configuration Release -sdk %s "
           "IPHONEOS_DEPLOYMENT_TARGET=%s SYMROOT=%s/build/ios/sdl OBJROOT=%s/build/ios/sdl/obj build"
           % (sdl_proj, SDK, MIN_IOS, t.root, t.root), description="SDL2 (iOS)", pool="console")
    n.build(t.sdl_lib(), "sdl_ios", implicit=[sdl_proj + "/project.pbxproj"])

    proj = write_xcode_project(t, out)
    app = out + "/xcode/build/Release-iphoneos/SonicRiders.app"
    # no Apple team on this Mac: an unsigned app; Sideloadly / AltStore sign the .ipa with the player's Apple ID
    sign = ("-allowProvisioningUpdates -allowProvisioningDeviceRegistration DEVELOPMENT_TEAM=%s" % t.team if t.team
            else "CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=")
    n.rule("xcode_app", "xcodebuild -quiet -project %s -configuration Release -sdk %s %s SYMROOT=%s/%s/xcode/build build"
           % (proj, SDK, sign, t.root, out), description="XCODE $out (link, bundle%s)" % (", sign" if t.team else ", unsigned"),
           pool="console")
    n.build(app + "/SonicRiders", "xcode_app", [lib, t.sdl_lib()],
            implicit=[proj + "/project.pbxproj", out + "/xcode/Info.plist"] + ([t.rel(t.icon)] if t.icon else []))
    # the .ipa (Payload/SonicRiders.app zipped) for sideloading tools that re-sign it (Sideloadly, AltStore)
    ipa = out + "/SonicRiders.ipa"
    n.rule("ipa", "%s tools/ios_build.py ipa %s $out %s%s" % (py, app, t.ipa_id, (' "%s"' % t.ipa_dir) if t.ipa_dir else ""),
           description="IPA $out")
    n.build(ipa, "ipa", app + "/SonicRiders", implicit=["tools/ios_build.py"])
    n.default(ipa)
    return app + "/SonicRiders"


def make_ipa(app, ipa, bundle_id, copy_dir=""):
    """zip the .app as Payload/SonicRiders.app with bundle_id (ipa_bundle_id; Sideloadly and AltStore re-sign it);
    optionally copy it to copy_dir (previous one kept as -prev)"""
    tmp = ipa + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "Payload"))
    dst_app = os.path.join(tmp, "Payload", os.path.basename(app))
    subprocess.check_call(["ditto", app, dst_app])
    subprocess.check_call(["plutil", "-replace", "CFBundleIdentifier", "-string", bundle_id,
                           os.path.join(dst_app, "Info.plist")])
    if os.path.exists(ipa + ".part"):
        os.remove(ipa + ".part")
    subprocess.check_call(["ditto", "-c", "-k", "--norsrc", "--keepParent", "Payload", os.path.abspath(ipa + ".part")], cwd=tmp)
    shutil.rmtree(tmp)
    os.replace(ipa + ".part", ipa)
    if copy_dir:
        os.makedirs(copy_dir, exist_ok=True)
        dst = os.path.join(copy_dir, "SonicRiders.ipa")
        if os.path.exists(dst):
            os.replace(dst, os.path.join(copy_dir, "SonicRiders-prev.ipa"))
        shutil.copy2(ipa, dst)
        print("ipa: %s" % dst)


INFO_PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>Sonic Riders</string>
  <key>CFBundleExecutable</key><string>$(EXECUTABLE_NAME)</string>
  <key>CFBundleIdentifier</key><string>$(PRODUCT_BUNDLE_IDENTIFIER)</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>Sonic Riders</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.5</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>UILaunchScreen</key><dict/>
  <key>UIRequiresFullScreen</key><true/>
  <key>UIStatusBarHidden</key><true/>
  <key>UIViewControllerBasedStatusBarAppearance</key><false/>
  <key>UISupportedInterfaceOrientations</key>
  <array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
  <key>UISupportedInterfaceOrientations~ipad</key>
  <array><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
  <key>UIFileSharingEnabled</key><true/>
  <key>LSSupportsOpeningDocumentsInPlace</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
  <key>GCSupportedGameControllers</key>
  <array><dict><key>ProfileName</key><string>ExtendedGamepad</string></dict></array>
  <key>UIBackgroundModes</key><array/>
  <key>UIApplicationSceneManifest</key>
  <dict>
    <key>UIApplicationSupportsMultipleScenes</key><false/>
    <key>UISceneConfigurations</key>
    <dict><key>UIWindowSceneSessionRoleApplication</key>
      <array><dict><key>UISceneConfigurationName</key><string>Default</string></dict></array></dict>
  </dict>
</dict>
</plist>
"""


def write_xcode_project(t, out):
    """a one-target Xcode project: no sources of its own; it links libsonicriders.a (force-loaded: main(),
    SDL_main and the recompiled code), SDL2 and MoltenVK into the app and signs it"""
    xdir = os.path.join(t.root, out, "xcode")
    pdir = os.path.join(xdir, "SonicRiders.xcodeproj")
    os.makedirs(pdir, exist_ok=True)
    with open(os.path.join(xdir, "Info.plist"), "w", encoding="utf-8") as f:
        f.write(INFO_PLIST)
    # a source file Xcode can compile (a target with an empty Sources phase links nothing on some versions)
    with open(os.path.join(xdir, "stub.c"), "w", encoding="utf-8") as f:
        f.write("/* the app's code is in libsonicriders.a (tools/ios_build.py) */\nint sr_ios_stub;\n")
    ldflags = ["-force_load", "$(PROJECT_DIR)/../libsonicriders.a", "$(PROJECT_DIR)/../sdl/Release-iphoneos/libSDL2.a",
               t.mvk_lib, "-lc++", "-Wl,-u,_vkGetInstanceProcAddr"]
    for fw in FRAMEWORKS:
        ldflags += ["-framework", fw]

    def q(s):
        return '"%s"' % s.replace('"', '\\"')
    icon_set = os.path.join(xdir, "Assets.xcassets", "AppIcon.appiconset")
    os.makedirs(icon_set, exist_ok=True)
    with open(os.path.join(xdir, "Assets.xcassets", "Contents.json"), "w", encoding="utf-8") as f:
        f.write('{"info": {"author": "xcode", "version": 1}}\n')
    images = []
    if t.icon:
        # iOS app icons are opaque: one 1024 x 1024 picture, transparent parts filled (Xcode makes the sizes)
        tmp = os.path.join(xdir, "icon_1024_alpha.png")
        square_png(t.icon, tmp)
        flatten_png(tmp, os.path.join(icon_set, "icon_1024.png"), ICON_BACKGROUND)
        images = [{"filename": "icon_1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}]
    import json
    with open(os.path.join(icon_set, "Contents.json"), "w", encoding="utf-8") as f:
        json.dump({"images": images, "info": {"author": "xcode", "version": 1}}, f)
    ld = "(\n" + "".join("\t\t\t\t\t%s,\n" % q(x) for x in ldflags) + "\t\t\t\t)"
    target_settings = {
        "PRODUCT_NAME": "SonicRiders", "PRODUCT_BUNDLE_IDENTIFIER": t.bundle_id, "INFOPLIST_FILE": "Info.plist",
        "IPHONEOS_DEPLOYMENT_TARGET": MIN_IOS, "TARGETED_DEVICE_FAMILY": q("1,2"), "SDKROOT": "iphoneos",
        "ARCHS": "arm64", "CODE_SIGN_STYLE": "Automatic", "CODE_SIGN_IDENTITY": q("Apple Development"),
        "GENERATE_INFOPLIST_FILE": "NO", "ENABLE_BITCODE": "NO", "DEAD_CODE_STRIPPING": "YES",
        "DEBUG_INFORMATION_FORMAT": "dwarf-with-dsym", "OTHER_LDFLAGS": ld, "SUPPORTS_MACCATALYST": "NO",
    }
    if t.team:
        target_settings["DEVELOPMENT_TEAM"] = t.team
    if t.icon:
        target_settings["ASSETCATALOG_COMPILER_APPICON_NAME"] = "AppIcon"
    ts = "".join("\t\t\t\t%s = %s;\n" % (k, v) for k, v in target_settings.items())
    pbx = PBXPROJ.replace("@TARGET_SETTINGS@", ts)
    with open(os.path.join(pdir, "project.pbxproj"), "w", encoding="utf-8") as f:
        f.write(pbx)
    return t.rel(pdir)


ICON_BACKGROUND = (0x0B, 0x3D, 0x91)  # deep blue behind a round icon's transparent corners
# pictures macOS's sips reads (GIF: the first frame)
ICON_EXTS = (".png", ".jpg", ".jpeg", ".heic", ".heif", ".webp", ".gif", ".bmp", ".tif", ".tiff")


def square_png(src, dst, size=1024):
    """any picture sips reads -> a size x size PNG: the centre square of it (no stretching), then scaled"""
    info = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", src], capture_output=True, text=True)
    dims = re.findall(r"pixel(?:Width|Height):\s*(\d+)", info.stdout)
    if info.returncode or len(dims) != 2:
        sys.exit("icon: macOS cannot read %s as a picture" % src)
    side = min(int(dims[0]), int(dims[1]))
    # two passes: in one call sips scales before it crops
    subprocess.check_call(["sips", "-s", "format", "png", "--cropToHeightWidth", str(side), str(side), src,
                           "--out", dst], stdout=subprocess.DEVNULL)
    subprocess.check_call(["sips", "-z", str(size), str(size), dst], stdout=subprocess.DEVNULL)


def flatten_png(src, dst, bg):
    """a non-interlaced PNG (8 or 16 bit; grey, RGB, palette, with or without alpha) -> an opaque 8-bit RGB PNG
    over the colour bg (no PIL needed)"""
    data = open(src, "rb").read()
    pos, idat, hdr, plte, trns = 8, b"", None, b"", b""
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        pos += 12 + n
    w, h, depth, ctype, _, _, interlace = hdr
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if not chans or interlace or depth not in ((8,) if ctype == 3 else (8, 16)):
        sys.exit("icon: unsupported PNG (depth %d, colour type %d, interlace %d)" % (depth, ctype, interlace))
    bpp = chans * depth // 8
    raw, stride = zlib.decompress(idat), w * bpp
    prev, out = bytearray(stride), bytearray()
    for y in range(h):
        ft, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):  # undo the PNG row filter
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if ft == 1: line[i] = (line[i] + a) & 255
            elif ft == 2: line[i] = (line[i] + b) & 255
            elif ft == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        out.append(0)
        for x in range(w):
            px = line[x * bpp:x * bpp + bpp:depth // 8]  # 16-bit: the high bytes
            if ctype == 3:
                i = px[0]
                rgb, al = plte[3 * i:3 * i + 3], trns[i] if i < len(trns) else 255
            else:
                rgb = px[:3] if ctype in (2, 6) else px[:1] * 3
                al = px[-1] if ctype in (4, 6) else 255
            out += bytes((rgb[k] * al + bg[k] * (255 - al)) // 255 for k in range(3))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(out), 9)) + chunk(b"IEND", b""))


# object IDs: fixed 24-digit hex strings (any unique values work)
PBXPROJ = """// !$*UTF8*$!
{
	archiveVersion = 1;
	classes = {
	};
	objectVersion = 56;
	objects = {
		A10000000000000000000001 /* stub.c in Sources */ = {isa = PBXBuildFile; fileRef = A10000000000000000000002; };
		A10000000000000000000002 /* stub.c */ = {isa = PBXFileReference; lastKnownFileType = sourcecode.c.c; path = stub.c; sourceTree = "<group>"; };
		A10000000000000000000003 /* Info.plist */ = {isa = PBXFileReference; lastKnownFileType = text.plist.xml; path = Info.plist; sourceTree = "<group>"; };
		A10000000000000000000005 /* Assets.xcassets in Resources */ = {isa = PBXBuildFile; fileRef = A10000000000000000000006; };
		A10000000000000000000006 /* Assets.xcassets */ = {isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = Assets.xcassets; sourceTree = "<group>"; };
		A10000000000000000000004 /* SonicRiders.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = SonicRiders.app; sourceTree = BUILT_PRODUCTS_DIR; };
		A10000000000000000000010 = {isa = PBXGroup; children = (A10000000000000000000002, A10000000000000000000003, A10000000000000000000006, A10000000000000000000011); sourceTree = "<group>"; };
		A10000000000000000000011 /* Products */ = {isa = PBXGroup; children = (A10000000000000000000004); name = Products; sourceTree = "<group>"; };
		A10000000000000000000020 /* Sources */ = {isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = (A10000000000000000000001); runOnlyForDeploymentPostprocessing = 0; };
		A10000000000000000000021 /* Resources */ = {isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = (A10000000000000000000005); runOnlyForDeploymentPostprocessing = 0; };
		A10000000000000000000030 /* SonicRiders */ = {
			isa = PBXNativeTarget;
			buildConfigurationList = A10000000000000000000041;
			buildPhases = (A10000000000000000000020, A10000000000000000000021);
			buildRules = (
			);
			dependencies = (
			);
			name = SonicRiders;
			productName = SonicRiders;
			productReference = A10000000000000000000004;
			productType = "com.apple.product-type.application";
		};
		A10000000000000000000031 /* Project object */ = {
			isa = PBXProject;
			attributes = {LastUpgradeCheck = 1600; };
			buildConfigurationList = A10000000000000000000040;
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (en, Base);
			mainGroup = A10000000000000000000010;
			productRefGroup = A10000000000000000000011;
			projectDirPath = "";
			projectRoot = "";
			targets = (A10000000000000000000030);
		};
		A10000000000000000000050 /* Release (project) */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
				SDKROOT = iphoneos;
				IPHONEOS_DEPLOYMENT_TARGET = 16.0;
			};
			name = Release;
		};
		A10000000000000000000051 /* Release (target) */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
@TARGET_SETTINGS@			};
			name = Release;
		};
		A10000000000000000000040 = {isa = XCConfigurationList; buildConfigurations = (A10000000000000000000050); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; };
		A10000000000000000000041 = {isa = XCConfigurationList; buildConfigurations = (A10000000000000000000051); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; };
	};
	rootObject = A10000000000000000000031;
}
"""


def make_icon(src, dst):
    """your picture -> the app's 1024 x 1024 opaque icon PNG (as the build makes it; the launcher's preview)"""
    tmp = dst + ".alpha.png"
    square_png(src, tmp)
    flatten_png(tmp, dst, ICON_BACKGROUND)
    os.remove(tmp)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "icon":  # ios_build.py icon <picture> <out.png>
        make_icon(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 5 and sys.argv[1] == "ipa":  # from build.ninja: ios_build.py ipa <app> <out.ipa> <id> [copy dir]
        make_ipa(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5] if len(sys.argv) > 5 else "")
    else:
        sys.exit("usage: ios_build.py ipa <SonicRiders.app> <out.ipa> <bundle id> [copy dir] | icon <picture> <out.png>")
