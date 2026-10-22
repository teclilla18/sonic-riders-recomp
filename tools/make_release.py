#!/usr/bin/env python3
"""Make the release zip: the committed source plus the setup tool, nothing else.

  python tools/make_release.py [--version 0.1.0] [--ref HEAD] [--out build/release]

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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="dev", help="version in the zip name (default: dev)")
    ap.add_argument("--ref", default="HEAD", help="commit to package (default: HEAD)")
    ap.add_argument("--out", default=os.path.join("build", "release"), help="output folder")
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
    top = "sonic-riders-recomp-%s" % args.version
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "%s-%s.zip" % (top, commit))
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
