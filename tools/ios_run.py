"""iPhone test runs from the Mac (the iOS counterpart of tools/android_run.py), over Xcode's devicectl.

  python3 tools/ios_run.py devices [--tsv]         list connected devices (the one on the cable first)
  python3 tools/ios_run.py install [--move]        install build/ios/xcode/build/Release-iphoneos/SonicRiders.app
                                                   (an older Sonic Riders under another app ID is moved: see below)
  python3 tools/ios_run.py status                  what install would do: "status: update|new|move <old ID>"
  python3 tools/ios_run.py restore <folder>        copy a backup made by a move into the installed app
  python3 tools/ios_run.py push-iso <image.iso>    copy the disc image to Documents/baserom/ (once; ~1.4 GB)
  python3 tools/ios_run.py run [KEY=VALUE ...]     launch with the console attached (stdout/stderr on the Mac);
                                                   KEY=VALUE pairs become environment variables (SR_*)
  python3 tools/ios_run.py pull [name ...]         copy Documents/<name> back to build/ios/pulled/
                                                   (default: sonicriders.log, sonicriders.prev.log, crash.txt)

--device <id or name> picks a device when more than one is connected. Everything the app writes is in its
Documents folder (Files app: On My iPhone > Sonic Riders). docs/notes/ios.md.

One app ID per player: net.srnative.sonicriders.<team ID>, the ID Sideloadly's default settings make too. An app
installed under an older ID (net.srnative.sonicriders, .t<hash>) is MOVED by install: its Documents (saves, settings,
disc image) are copied to build/ios/moved/<old ID>-<time>/, checked, the old app is removed (a free Apple ID allows
only 3 apps per device), the new one installed and the files copied back and checked. Asks first (--move: yes).
"""
import datetime
import json
import os
import re
import shutil
import plistlib
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, "build/ios/xcode/build/Release-iphoneos/SonicRiders.app")
BASE_ID = "net.srnative.sonicriders"


def is_standard(bid):
    """net.srnative.sonicriders.<10-character team ID>: what the Mac builds and Sideloadly's default make"""
    return re.fullmatch(re.escape(BASE_ID) + r"\.[A-Z0-9]{10}", bid or "") is not None


def devicectl(*a, check=True):
    cmd = ["xcrun", "devicectl"] + list(a)
    print("+ " + " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=check).returncode


def devices():
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "devices.json")
        subprocess.run(["xcrun", "devicectl", "list", "devices", "--json-output", out], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(out) as f:
            data = json.load(f)
    found = []
    for dev in data.get("result", {}).get("devices", []):
        props = dev.get("deviceProperties", {})
        hw = dev.get("hardwareProperties", {})
        found.append({"id": dev.get("identifier", ""), "name": props.get("name", "?"), "udid": hw.get("udid", ""),
                      "os": props.get("osVersionNumber", "?"), "model": hw.get("marketingName", "?"),
                      # the tunnel reads "disconnected" while idle; devicectl opens it on demand
                      "state": "%s, tunnel %s" % (dev.get("connectionProperties", {}).get("pairingState", "?"),
                                                  dev.get("connectionProperties", {}).get("tunnelState", "?")),
                      "wired": dev.get("connectionProperties", {}).get("transportType") == "wired"})
    found.sort(key=lambda d: not d["wired"])  # the device on the cable first (the one being set up)
    return found


def installed_ids(dev):
    """bundle IDs of the Sonic Riders apps already on the device (net.srnative.sonicriders*), sorted"""
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "apps.json")
        subprocess.run(["xcrun", "devicectl", "device", "info", "apps", "--device", dev, "--json-output", out],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(out) as f:
            apps = json.load(f).get("result", {}).get("apps", [])
    return sorted(a.get("bundleIdentifier", "") for a in apps
                  if a.get("bundleIdentifier", "").startswith("net.srnative.sonicriders"))


def documents(dev, bid):
    """[(relative path, size)] of the files in an app's Documents folder; None when it can't be read"""
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "files.json")
        r = subprocess.run(["xcrun", "devicectl", "device", "info", "files", "--device", dev, "--domain-type",
                            "appDataContainer", "--domain-identifier", bid, "--subdirectory", "Documents",
                            "--json-output", out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode or not os.path.isfile(out):
            return None
        with open(out) as f:
            files = json.load(f).get("result", {}).get("files", [])
    return sorted((f["relativePath"], f.get("metadata", {}).get("size", 0)) for f in files
                  if not f.get("resources", {}).get("isDirectory"))


def plan(dev, new):
    """what installing app ID `new` does on the device: ("update"|"new"|"move", the installed app it replaces)"""
    ids = installed_ids(dev)
    if new in ids:
        return "update", new
    if not ids:
        return "new", ""
    return "move", next((i for i in ids if not is_standard(i)), ids[0])


def copy_in(dev, bid, folder):
    """copy every file under `folder` into the app's Documents; returns the files that did not arrive intact"""
    want = []
    for top, _, names in os.walk(folder):
        for n in names:
            full = os.path.join(top, n)
            rel = os.path.relpath(full, folder).replace(os.sep, "/")
            want.append((rel, os.path.getsize(full)))
            devicectl("device", "copy", "to", "--device", dev, "--domain-type", "appDataContainer",
                      "--domain-identifier", bid, "--source", full, "--destination", "Documents/" + rel, check=False)
    have = dict(documents(dev, bid) or [])
    return [rel for rel, size in want if have.get(rel) != size]


def profile_problem(dev):
    """why the built app cannot be installed on the device, or "" when its provisioning profile lists the device and
    is still valid. A free Apple ID's profile names each device; Xcode reuses a cached one that may lack a device
    registered later ("This provisioning profile cannot be installed on this device", 2026-11-13)."""
    prof = os.path.join(APP, "embedded.mobileprovision")
    if not os.path.isfile(prof):
        return "the app has no provisioning profile (an unsigned build): install it with Sideloadly instead"
    info = profile_info(prof)
    if info is None:
        return "could not read the app's provisioning profile"
    udid = next((d["udid"] for d in devices() if d["id"] == dev), "")
    listed = info.get("ProvisionedDevices")
    if listed is not None and (not udid or udid.lower() not in (u.lower() for u in listed)):
        return ("the app's provisioning profile does not list this device. Make Xcode create a new one: move the "
                "file for this app ID out of ~/Library/Developer/Xcode/UserData/Provisioning Profiles, delete %s "
                "and run ninja with the device connected" % os.path.relpath(APP, ROOT))
    end = info.get("ExpirationDate")
    if end is not None and end.replace(tzinfo=datetime.timezone.utc) <= datetime.datetime.now(datetime.timezone.utc):
        return "the app's provisioning profile ran out on %s: move its file out of ~/Library/Developer/Xcode/UserData/Provisioning Profiles and rebuild" % end
    return ""


PROFILES = os.path.expanduser("~/Library/Developer/Xcode/UserData/Provisioning Profiles")
FRESH_DAYS = 6.5  # a free Apple ID's profile lasts 7 days from when Xcode makes it


def profile_info(path):
    r = subprocess.run(["security", "cms", "-D", "-i", path], capture_output=True)
    try:
        return plistlib.loads(r.stdout)
    except Exception:
        return None


def days_left():
    """days until the built app's provisioning profile runs out (None: unsigned build or unreadable)"""
    info = profile_info(os.path.join(APP, "embedded.mobileprovision"))
    end = info and info.get("ExpirationDate")
    if not end:
        return None
    return (end.replace(tzinfo=datetime.timezone.utc) - datetime.datetime.now(datetime.timezone.utc)).total_seconds() / 86400


def refresh_profile(dev):
    """Renewing = a NEW profile: Xcode reuses its cached one until it runs out, so reinstalling the same build gives
    no extra days, and a cached profile can lack a device registered later (2026-11-13). When the built app's profile
    is not fresh or does not list the device, move this app ID's cached profiles to build/ios/old-profiles/, delete
    the app and rebuild it (only the signing step; Xcode then makes a new profile with every registered device)."""
    left = days_left()
    if left is None:
        return  # unsigned build: Sideloadly / AltStore sign it
    if left >= FRESH_DAYS and not profile_problem(dev):
        return
    print("== getting a fresh 7-day signature (%.1f days left on this build's)" % max(left, 0), flush=True)
    bid = bundle_id()
    keep = os.path.join(ROOT, "build", "ios", "old-profiles")
    if os.path.isdir(PROFILES):
        for name in os.listdir(PROFILES):
            path = os.path.join(PROFILES, name)
            info = profile_info(path) or {}
            appid = info.get("Entitlements", {}).get("application-identifier", "")
            if appid.split(".", 1)[-1] == bid:
                os.makedirs(keep, exist_ok=True)
                shutil.move(path, os.path.join(keep, name))
    shutil.rmtree(APP, ignore_errors=True)
    ninja = shutil.which("ninja") or next((p for p in ("/opt/homebrew/bin/ninja", "/usr/local/bin/ninja")
                                           if os.path.isfile(p)), "ninja")
    if subprocess.run([ninja], cwd=ROOT).returncode or not os.path.isdir(APP):
        sys.exit("could not rebuild the app for a fresh signature (run ninja in %s and look at the error)" % ROOT)
    left = days_left()
    print("== signature good for %.1f days" % (left or 0), flush=True)


def move(dev, old, new):
    """old app -> backup on the Mac -> remove old -> install new -> copy back. Stops before removing anything when
    the backup is incomplete; after that the backup folder is kept until everything is back on the device."""
    why = profile_problem(dev)  # checked first: once the old app is removed, a refused install leaves none
    if why:
        sys.exit("not moved, nothing was changed: " + why)
    files = documents(dev, old)
    if files is None:
        sys.exit("could not read the files of %s on the device (unlock it and try again); nothing was changed" % old)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(ROOT, "build", "ios", "moved", "%s-%s" % (old, stamp))
    print("== 1/4 copying %d file(s) of %s to %s" % (len(files), old, os.path.relpath(backup, ROOT)), flush=True)
    for rel, size in files:
        dst = os.path.join(backup, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        devicectl("device", "copy", "from", "--device", dev, "--domain-type", "appDataContainer",
                  "--domain-identifier", old, "--source", "Documents/" + rel, "--destination", dst, check=False)
        if not os.path.isfile(dst) or os.path.getsize(dst) != size:
            sys.exit("copy of %s from the device failed; nothing was removed (backup so far: %s)" % (rel, backup))
    print("== 2/4 removing the old app %s" % old, flush=True)
    devicectl("device", "uninstall", "app", "--device", dev, old)
    print("== 3/4 installing %s" % new, flush=True)
    if devicectl("device", "install", "app", "--device", dev, APP, check=False):
        sys.exit("install failed; your files are safe in %s. After fixing it: ios_run.py restore \"%s\"" % (backup, backup))
    print("== 4/4 copying the files into the new app", flush=True)
    bad = copy_in(dev, new, backup) if files else []
    if bad:
        sys.exit("these did not arrive: %s. Your files are safe in %s; try ios_run.py restore \"%s\""
                 % (", ".join(bad), backup, backup))
    for rel, _ in files:  # the disc image is the player's own file anyway: don't keep a 1.4 GB copy
        if rel.lower().endswith(".iso"):
            os.remove(os.path.join(backup, *rel.split("/")))
    print("moved: %s -> %s (saves, settings and disc image kept; a copy of the saves stays in %s)"
          % (old, new, os.path.relpath(backup, ROOT)), flush=True)


def ask_yes(question):
    if not sys.stdin.isatty():
        return False
    return input(question + " (y/n) ").strip().lower().startswith("y")


def install(dev, allow_move):
    refresh_profile(dev)
    new = bundle_id()
    what, old = plan(dev, new)
    if what != "move":
        print("Sonic Riders %s: %s" % ("is updated in place" if what == "update" else "is new on this device", new),
              flush=True)
        devicectl("device", "install", "app", "--device", dev, APP)
        return
    print("This device has Sonic Riders under an older app ID (%s); this build is %s." % (old, new))
    print("Installing moves it: saves, settings and the disc image come along, then the old app is removed.")
    print("(A free Apple ID allows 3 apps per device; renewals with Sideloadly's default settings reach the new ID.)")
    if not (allow_move or ask_yes("Move it now?")):
        sys.exit("not installed. Run again with --move (the launcher's Install button does) to move it.")
    move(dev, old, new)


def pick_device(want):
    devs = devices()
    if want:
        devs = [d for d in devs if want in (d["id"], d["name"])]
    if not devs:
        sys.exit("no device found: connect the iPhone by USB, unlock it and tap Trust; Developer Mode must be on")
    if len(devs) > 1 and not want and sum(d["wired"] for d in devs) == 1:
        return devs[0]["id"]  # several paired, one on the cable: that one
    if len(devs) > 1:
        sys.exit("more than one device; pick one with --device: " + ", ".join("%s (%s)" % (d["name"], d["id"]) for d in devs))
    return devs[0]["id"]


def bundle_id():
    if not os.path.isdir(APP):
        sys.exit("no app built yet (%s): run ninja first" % os.path.relpath(APP, ROOT))
    return subprocess.run(["plutil", "-extract", "CFBundleIdentifier", "raw", os.path.join(APP, "Info.plist")],
                          check=True, capture_output=True, text=True).stdout.strip()


def main():
    args = sys.argv[1:]
    want = ""
    if "--device" in args:
        i = args.index("--device")
        want = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    cmd, rest = args[0], args[1:]
    if cmd == "devices":
        if "--tsv" in rest:  # for the Mac launcher: id, name, model, cable
            for d in devices():
                if "paired" in d["state"]:
                    print("\t".join([d["id"], d["name"], d["model"], "cable" if d["wired"] else ""]))
            return
        for d in devices():
            print("%-40s %-24s %-18s iOS %-8s %s%s" % (d["id"], d["name"], d["model"], d["os"], d["state"],
                                                      ", cable" if d["wired"] else ""))
        return
    dev = pick_device(want)
    if cmd == "install":
        install(dev, "--move" in rest or "--yes" in rest)
    elif cmd == "status":
        new = bundle_id()
        what, old = plan(dev, new)
        disc = what == "update" and any(r.lower().endswith(".iso") for r, _ in (documents(dev, new) or []))
        print("app: %s" % new)
        print("installed: %s" % (", ".join(installed_ids(dev)) or "-"))
        print("status: %s%s" % (what, " " + old if what == "move" else ""))
        print("disc: %s" % ("yes" if disc else "no"))
    elif cmd == "restore":
        if not rest or not os.path.isdir(rest[0]):
            sys.exit("usage: ios_run.py restore <folder made by a move>")
        bad = copy_in(dev, bundle_id(), rest[0])
        sys.exit("these did not arrive: " + ", ".join(bad) if bad else 0)
    elif cmd == "push-iso":
        if not rest or not os.path.isfile(rest[0]):
            sys.exit("usage: ios_run.py push-iso <image.iso>")
        size = os.path.getsize(rest[0])
        if any(r.lower().endswith(".iso") and n == size for r, n in (documents(dev, bundle_id()) or [])):
            print("the disc image is already on the device: not copied again")
            return
        devicectl("device", "copy", "to", "--device", dev, "--domain-type", "appDataContainer",
                  "--domain-identifier", bundle_id(), "--source", rest[0],
                  "--destination", "Documents/baserom/" + os.path.basename(rest[0]))
    elif cmd == "run":
        env = dict(kv.split("=", 1) for kv in rest if "=" in kv)
        extra = ["--environment-variables", json.dumps(env)] if env else []
        rc = devicectl("device", "process", "launch", "--device", dev, "--console", "--terminate-existing",
                       *extra, bundle_id(), check=False)
        if rc:  # e.g. the phone is locked or the developer is not trusted yet
            sys.exit(rc)
    elif cmd == "pull":
        names = rest or ["sonicriders.log", "sonicriders.prev.log", "crash.txt"]
        out = os.path.join(ROOT, "build/ios/pulled")
        os.makedirs(out, exist_ok=True)
        for name in names:
            devicectl("device", "copy", "from", "--device", dev, "--domain-type", "appDataContainer",
                      "--domain-identifier", bundle_id(), "--source", "Documents/" + name,
                      "--destination", os.path.join(out, os.path.basename(name)), check=False)
        print("pulled into", os.path.relpath(out, ROOT))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
