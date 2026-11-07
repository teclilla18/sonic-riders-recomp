"""iOS build pieces for configure.py (--target ios), on a Mac with Xcode. docs/notes/ios.md.

ninja compiles the runtime and the recompiled code with Xcode's clang into build/ios/libsonicriders.a, builds SDL2's
static library from its source with SDL's own Xcode project, and then runs xcodebuild on a small generated project
(build/ios/xcode/) that only links, bundles and signs: Xcode makes the provisioning profile a free Apple account
needs (Personal Team, 7-day builds). Nothing here is game data; the .app it makes is (never publish it).
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys

MIN_IOS = "16.0"
SDK = "iphoneos"
FRAMEWORKS = ["UIKit", "Foundation", "QuartzCore", "Metal", "IOSurface", "CoreGraphics", "CoreFoundation",
              "AVFoundation", "AudioToolbox", "CoreAudio", "CoreMotion", "CoreHaptics", "GameController",
              "CoreBluetooth", "IOKit"]


def _run(cmd):
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


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
    return ""


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
        self.team = args.team or find_team()
        # the bundle ID must be unique across Apple: a per-team suffix keeps it out of the repository
        self.bundle_id = args.bundle_id or ("net.srnative.sonicriders.t" + hashlib.sha1(self.team.encode()).hexdigest()[:8]
                                            if self.team else "net.srnative.sonicriders")

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
    team = "DEVELOPMENT_TEAM=%s" % t.team if t.team else ""
    n.rule("xcode_app", "xcodebuild -quiet -project %s -configuration Release -sdk %s -allowProvisioningUpdates "
           "-allowProvisioningDeviceRegistration %s SYMROOT=%s/%s/xcode/build build"
           % (proj, SDK, team, t.root, out), description="XCODE $out (link, bundle, sign)", pool="console")
    n.build(app + "/SonicRiders", "xcode_app", [lib, t.sdl_lib()],
            implicit=[proj + "/project.pbxproj", out + "/xcode/Info.plist"])
    return app + "/SonicRiders"


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
    ts = "".join("\t\t\t\t%s = %s;\n" % (k, v) for k, v in target_settings.items())
    pbx = PBXPROJ.replace("@TARGET_SETTINGS@", ts)
    with open(os.path.join(pdir, "project.pbxproj"), "w", encoding="utf-8") as f:
        f.write(pbx)
    return t.rel(pdir)


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
		A10000000000000000000004 /* SonicRiders.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = SonicRiders.app; sourceTree = BUILT_PRODUCTS_DIR; };
		A10000000000000000000010 = {isa = PBXGroup; children = (A10000000000000000000002, A10000000000000000000003, A10000000000000000000011); sourceTree = "<group>"; };
		A10000000000000000000011 /* Products */ = {isa = PBXGroup; children = (A10000000000000000000004); name = Products; sourceTree = "<group>"; };
		A10000000000000000000020 /* Sources */ = {isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = (A10000000000000000000001); runOnlyForDeploymentPostprocessing = 0; };
		A10000000000000000000030 /* SonicRiders */ = {
			isa = PBXNativeTarget;
			buildConfigurationList = A10000000000000000000041;
			buildPhases = (A10000000000000000000020);
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
