#!/usr/bin/env python3
"""One Android test run over adb (docs/ANDROID_PLAN.md, section 8): install, start with launch arguments,
capture the app's own log until a line appears (or the app exits / times out), pull files from the app's
folder.

  python tools/android_run.py --apk build/android/probe-arm64-v8a.apk --package net.srnative.sonicriders.probe \
      --wait "\\[probe\\] done" --pull probe.log --pull probe_gles.bmp [--args "PROBE_DRAWS=7000"] [--serial S]

Privacy: only this app's process is logged (logcat --pid) plus crash reports that name the package, and
only files in the app's own folder (/sdcard/Android/data/<package>/files) are read. Output goes to
build/android/run/ (never committed; logs can name the device).
"""
import argparse
import os
import queue
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACTIVITY = "net.srnative.sonicriders.SonicRidersActivity"


def default_adb():
    base = os.environ.get("LOCALAPPDATA", os.path.expanduser("~"))
    p = os.path.join(base, "SonicRidersSetup", "tools", "android-sdk", "platform-tools", "adb.exe" if os.name == "nt" else "adb")
    return p if os.path.isfile(p) else "adb"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", help="install this first (adb install -r)")
    ap.add_argument("--package", required=True)
    ap.add_argument("--args", default="", help="launch arguments (KEY=VALUE words become environment variables)")
    ap.add_argument("--wait", default="", help="regex: stop when a log line matches")
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--pull", action="append", default=[], help="file in the app's folder to copy back")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "android", "run"))
    ap.add_argument("--serial", default=os.environ.get("ANDROID_SERIAL", ""))
    ap.add_argument("--adb", default=default_adb())
    ap.add_argument("--keep-running", action="store_true", help="do not stop the app at the end")
    a = ap.parse_args()
    adb = [a.adb] + (["-s", a.serial] if a.serial else [])

    def sh(*cmd, check=True, capture=True):
        r = subprocess.run(adb + list(cmd), capture_output=capture, text=True, encoding="utf-8", errors="replace")
        if check and r.returncode:
            sys.exit("adb %s failed: %s" % (" ".join(cmd[:2]), (r.stderr or r.stdout).strip()))
        return r.stdout if capture else ""

    os.makedirs(a.out, exist_ok=True)
    if a.apk:
        print("installing", a.apk, flush=True)
        sh("install", "-r", a.apk)
    files = "/sdcard/Android/data/%s/files" % a.package
    sh("shell", "am", "force-stop", a.package)
    for name in a.pull:  # no stale results from an earlier run
        sh("shell", "rm", "-f", "%s/%s" % (files, name), check=False)
    sh("logcat", "-c", check=False)
    start = ["shell", "am", "start", "-W", "-n", "%s/%s" % (a.package, ACTIVITY)]
    if a.args:
        start += ["--es", "args", "'%s'" % a.args.replace("'", "")]
    print(sh(*start).strip(), flush=True)
    pid = ""
    t0 = time.time()
    while not pid and time.time() - t0 < 15:
        pid = sh("shell", "pidof", a.package, check=False).strip()
        if not pid:
            time.sleep(0.3)
    if not pid:
        sys.exit("the app did not start")
    print("pid", pid, flush=True)
    log_path = os.path.join(a.out, "logcat.txt")
    proc = subprocess.Popen(adb + ["logcat", "-v", "time", "--pid=" + pid], stdout=subprocess.PIPE, text=True,
                            encoding="utf-8", errors="replace")
    lines = queue.Queue()  # a reader thread: a pipe read blocks on Windows, and the app may go quiet or exit
    threading.Thread(target=lambda: [lines.put(l) for l in proc.stdout], daemon=True).start()
    pat = re.compile(a.wait) if a.wait else None
    found = False
    with open(log_path, "w", encoding="utf-8") as log:
        deadline = time.time() + a.timeout
        last_alive = time.time()
        while time.time() < deadline and not found:
            try:
                line = lines.get(timeout=0.5)
            except queue.Empty:
                line = None
            if line:
                log.write(line)
                log.flush()
                sys.stdout.write(line)
                found = bool(pat and pat.search(line))
                continue
            if time.time() - last_alive > 2:
                last_alive = time.time()
                if not sh("shell", "pidof", a.package, check=False).strip():
                    print("the app has exited", flush=True)
                    break
    proc.kill()
    # the whole log of that process from the buffer (the live stream misses lines when the app ends quickly)
    full = sh("logcat", "-d", "-v", "time", "--pid=" + pid, check=False)
    if full.strip():
        with open(log_path, "w", encoding="utf-8") as f:
            f.write(full)
        if not found and pat:
            found = any(pat.search(l) for l in full.splitlines())
    # a crash report from the system's crash dumper, if it names this package
    crash = sh("logcat", "-d", "-v", "time", "-s", "DEBUG:F", "AndroidRuntime:E", check=False)
    crash = "\n".join(l for l in crash.splitlines() if a.package in l or "backtrace" in l or re.search(r"#\d\d pc ", l))
    if crash.strip():
        with open(os.path.join(a.out, "crash.txt"), "w", encoding="utf-8") as f:
            f.write(crash + "\n")
        print("crash report saved to", os.path.join(a.out, "crash.txt"))
    time.sleep(0.5)
    for name in a.pull:
        dst = os.path.join(a.out, os.path.basename(name))
        r = subprocess.run(adb + ["pull", "%s/%s" % (files, name), dst], capture_output=True, text=True)
        print(("pulled %s" % dst) if r.returncode == 0 else ("could not pull %s" % name), flush=True)
    if not a.keep_running:
        sh("shell", "am", "force-stop", a.package, check=False)
    if pat and not found:
        print("did not see /%s/ within %.0f s" % (a.wait, a.timeout))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
