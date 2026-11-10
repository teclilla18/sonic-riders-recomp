"""iPhone test runs from the Mac (the iOS counterpart of tools/android_run.py), over Xcode's devicectl.

  python3 tools/ios_run.py devices                 list connected devices
  python3 tools/ios_run.py install                 install build/ios/xcode/build/Release-iphoneos/SonicRiders.app
  python3 tools/ios_run.py push-iso <image.iso>    copy the disc image to Documents/baserom/ (once; ~1.4 GB)
  python3 tools/ios_run.py run [KEY=VALUE ...]     launch with the console attached (stdout/stderr on the Mac);
                                                   KEY=VALUE pairs become environment variables (SR_*)
  python3 tools/ios_run.py pull [name ...]         copy Documents/<name> back to build/ios/pulled/
                                                   (default: sonicriders.log, sonicriders.prev.log, crash.txt)

--device <id or name> picks a device when more than one is connected. Everything the app writes is in its
Documents folder (Files app: On My iPhone > Sonic Riders). docs/notes/ios.md.
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, "build/ios/xcode/build/Release-iphoneos/SonicRiders.app")


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
        found.append({"id": dev.get("identifier", ""), "name": props.get("name", "?"),
                      "os": props.get("osVersionNumber", "?"), "model": hw.get("marketingName", "?"),
                      # the tunnel reads "disconnected" while idle; devicectl opens it on demand
                      "state": "%s, tunnel %s" % (dev.get("connectionProperties", {}).get("pairingState", "?"),
                                                  dev.get("connectionProperties", {}).get("tunnelState", "?"))})
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


def pick_device(want):
    devs = devices()
    if want:
        devs = [d for d in devs if want in (d["id"], d["name"])]
    if not devs:
        sys.exit("no device found: connect the iPhone by USB, unlock it and tap Trust; Developer Mode must be on")
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
        for d in devices():
            print("%-40s %-24s %-18s iOS %-8s %s" % (d["id"], d["name"], d["model"], d["os"], d["state"]))
        return
    dev = pick_device(want)
    if cmd == "install":
        devicectl("device", "install", "app", "--device", dev, APP)
    elif cmd == "push-iso":
        if not rest or not os.path.isfile(rest[0]):
            sys.exit("usage: ios_run.py push-iso <image.iso>")
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
