#!/usr/bin/env python3
"""The setup's build folder, kept between updates so an update rebuilds only what changed.

  build_workdir.py sync ROOT WORK        copy the build's inputs from the release folder ROOT into WORK
  build_workdir.py dtk WORK DTK          run decomp-toolkit's analysis in WORK unless its inputs are unchanged

Why: every release zip gives all its files one date (tools/make_release.py), newer than the last build, so
ninja would recompile the whole game after every update, and a new folder has no build at all. WORK (in the
setup's tools folder) keeps the converted game code and the compiled objects. `sync` copies only files whose
contents differ (they get today's date, so ninja rebuilds exactly those) and removes files that a newer release
dropped. Unchanged game-code inputs keep their old dates, so the game's ~minutes of compiling is skipped;
a release that changes the code converter (tools/recomp/, config/) still rebuilds it, as it must.

WORK holds the game's converted code: it stays on this computer, like build/ (never share it).
"""
import hashlib
import os
import shutil
import subprocess
import sys

# what configure.py and the build read from the release folder (not docs, the launcher or saves)
INPUTS = ("configure.py", "VERSION", "android", "config", "runtime", "setup", "third_party", "tools")
SKIP_DIRS = {"__pycache__", ".git"}
MANIFEST = "synced_files.txt"


def release_files(root):
    out = []
    for top in INPUTS:
        p = os.path.join(root, top)
        if os.path.isfile(p):
            out.append(top)
        elif os.path.isdir(p):
            for d, dirs, files in os.walk(p):
                dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
                for f in sorted(files):
                    if f.endswith(".pyc"):
                        continue
                    out.append(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
    return out


def same(a, b):
    if not os.path.isfile(b) or os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(1 << 20), fb.read(1 << 20)
            if x != y:
                return False
            if not x:
                return True


def sync(root, work):
    root, work = os.path.abspath(root), os.path.abspath(work)
    if os.path.normcase(root) == os.path.normcase(work):
        sys.exit("build_workdir: ROOT and WORK are the same folder")
    os.makedirs(work, exist_ok=True)
    files = release_files(root)
    if "configure.py" not in files:
        sys.exit("build_workdir: %s is not a release folder (no configure.py)" % root)
    changed = 0
    for rel in files:
        src, dst = os.path.join(root, rel), os.path.join(work, rel)
        if same(src, dst):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)  # copyfile, not copy2: the copy gets today's date, so ninja rebuilds it
        if os.access(src, os.X_OK) and os.name != "nt":
            os.chmod(dst, os.stat(src).st_mode)
        changed += 1
    # files the last release had and this one does not (a removed source file would still be compiled)
    removed = 0
    mpath = os.path.join(work, MANIFEST)
    if os.path.isfile(mpath):
        keep = set(files)
        with open(mpath, encoding="utf-8") as f:
            for rel in f.read().split("\n"):
                p = os.path.join(work, rel)
                if rel and rel not in keep and os.path.isfile(p):
                    os.remove(p)
                    removed += 1
    with open(mpath, "w", encoding="utf-8") as f:
        f.write("\n".join(files) + "\n")
    print("build_workdir: %d of %d files new or changed, %d removed (build folder %s)"
          % (changed, len(files), removed, work))


def file_hash(h, path):
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)


def dtk(work, exe):
    """decomp-toolkit rewrites its outputs every run (new dates: the whole game would be converted again)"""
    h = hashlib.sha256()
    for p in ("config/GXEE8P/config.yml", "baserom/main.dol", "baserom/_Main.rel", exe):
        file_hash(h, os.path.join(work, p))
    stamp_path = os.path.join(work, "build", "dtk.stamp")
    outs = [os.path.join(work, "config", "GXEE8P", "symbols.txt"), os.path.join(work, "config", "GXEE8P", "_Main", "symbols.txt")]
    if os.path.isfile(stamp_path) and all(os.path.isfile(p) for p in outs):
        with open(stamp_path, encoding="utf-8") as f:
            if f.read().strip() == h.hexdigest():
                print("build_workdir: the game code analysis is up to date (skipped)")
                return 0
    r = subprocess.call([exe, "dol", "split", "config/GXEE8P/config.yml", "build/GXEE8P"], cwd=work)
    if r == 0:
        os.makedirs(os.path.dirname(stamp_path), exist_ok=True)
        with open(stamp_path, "w", encoding="utf-8") as f:
            f.write(h.hexdigest() + "\n")
    return r


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "sync":
        sync(a[1], a[2])
    elif len(a) == 3 and a[0] == "dtk":
        sys.exit(dtk(a[1], a[2]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
