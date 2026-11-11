#!/usr/bin/env python3
"""Refuse anything game-derived or private in git (used by the pre-commit hook and GitHub Actions).

  check_no_game_data.py            every tracked file (the working tree's index)
  check_no_game_data.py --staged   the files staged for commit (tools/git-hooks/pre-commit)
  check_no_game_data.py --history  every file version in every commit reachable from any ref

This repository holds only our own source, docs and tooling. Refused:
  - game-data / build directories (baserom/, build/, gen/, asm/, ...) and file types (disc images,
    DOL/REL, ADX/SFD/AFS, objects, NRO/NSO/NSP, APK/AAB/.so/.dex, shader binaries (dksh, SPIR-V), signing keystores, keys, saves, logs, screenshots);
  - files over 512 KB, binary files (NUL bytes);
  - the game's own files by hash (ISO, main.dol, _Main.rel, SonicR.str);
  - disassembly (decomp-toolkit asm lines) and recompiled C (the recompiler's per-instruction lines);
  - secrets written as key = hex, and personal home-folder paths.
Exit status 1 and one line per problem when something is refused.
"""
import hashlib
import os
import re
import subprocess
import sys

MAX_BYTES = 512 * 1024
DIRS = ("baserom/", "orig/", "game/", "build/", "asm/", "gen/", "extracted/", "saves/", "assets_out/",
        ".gradle/", "android/app/build/")
EXTS = (".iso", ".gcm", ".rvz", ".ciso", ".gcz", ".wbfs", ".tgc", ".dol", ".rel", ".str", ".plf", ".elf",
        ".map", ".adx", ".sfd", ".afs", ".bnr", ".thp", ".s", ".o", ".obj", ".a", ".lib", ".exe", ".dll",
        ".nro", ".nso", ".nsp", ".nca", ".xci", ".npdm", ".nacp", ".dksh", ".bin", ".keys", ".raw", ".gci",
        ".sav", ".log", ".bmp", ".tga", ".wav", ".dtm", ".dat", ".ipa",  # .ipa: the iOS app (recompiled code)
        # Android build products: they hold recompiled game code (or signing keys) and are built locally
        ".apk", ".aab", ".apks", ".idsig", ".so", ".dex", ".spv", ".keystore", ".jks")
# SHA-1 of the user's disc image and the files on it (Redump GXEE8P rev 0); never committable
GAME_SHA1 = {
    "ce0cecf57034bef8bab7d6f588ce92c85a849613": "the disc image",
    "e90a13504974b917a675d59f07014e0e817c5e5d": "main.dol",
    "1607fbb1ccabf116642c3c59ee9b21c304a1db26": "_Main.rel",
    "caf827eb8c510744c0825304f68ae89250bb11a3": "SonicR.str",
}
# patterns are built from pieces so this file does not match itself
ASM_LINE = re.compile(r"^/\* [0-9A-F]{8} [0-9A-F]{8}  ([0-9A-F]{2} ){4}\*/", re.M)          # dtk asm
RECOMP_LINE = re.compile(r"/\* 8[0-9A-F]{7}: [0-9A-F]{8} \*/")                                  # recompiled C
SECRET = re.compile(r"(?i)\b[a-z0-9_]*(?:" + "key" + r"|kek)[a-z0-9_]*\s*=\s*[0-9a-f]{32}\b")
HOME_PATH = re.compile(r"(?i)(?:[a-z]:[\\/]" + "users" + r"[\\/][^\\/\s]+|/c/" + "users" + r"/[^/\s]+|/home/[^/\s]+)")
# placeholders allowed in docs
HOME_OK = re.compile(r"(?i)(users[\\/](?:<[^>]+>|(?:you|name|username|me|public)\b)|/home/(?:<|(?:you|user|runner)\b))")


# The one exception to "no binary files" (the user's OK, 2026-11-03): the touch-control pictures, the
# maintainer's own art (not from the game), exactly these files with exactly this content. A new or changed
# picture is refused until its SHA-1 is put here. Also the launcher banner (the user's OK, 2026-11-07): public
# fan art the user chose, not from the game.
OWN_ART = {
    "docs/images/banner.jpg": "54c9de3ff1b1c31237fc9649449e1f4bdfae4f9b",  # README banner (fan art, user's choice, 2026-11-09)
    "launcher/banner.jpg": "a1d51fb7890668be5dfd126311b946aec8744339",
    "launcher/icon_256.png": "80541e60eb82d9440c001da3bc4c7240bda2b111",  # launcher icon (user's ChatGPT art, 2026-11-09)
    "launcher/icon_32.png": "24e3018e7e6651be4cbba8228f81364bf9d0a70e",
    "setup/icon.png": "b42362f551138986cd53dda64721510c101a80e2",  # game icon: cedry2kio's fan-made icon (user's OK, 2026-11-10; README credits)
    "setup/icon_256.jpg": "849b075587a60443ed7ac41ab4cbf3f61736b4a0",  # the same, 256 x 256 for the Switch NRO
    "android/touch_icons/boost_b.png": "e91d748d8a99c76b0aad985ea40ae1437c10e982",
    "android/touch_icons/drift_r.png": "45e1558994abaad8f85966ad7c8ff32ee5cde5ac",
    "android/touch_icons/jump_a.png": "d1ff0d81d64478b89993997059c635d42cd3c331",
    "android/touch_icons/stick_base.png": "7279efbd5b60980cf5af16cb5a1c6666f34a1455",
    "android/touch_icons/stick_knob.png": "181e6e6f4882716c1c743ee5fdd85286224dd6dc",
    "android/touch_icons/tornado_lr.png": "5ab1c2552a4a15757d974abf7aa8e87226a2b441",
}


def check(path, data):
    if OWN_ART.get(path) == hashlib.sha1(data).hexdigest() and (data[:8] == b"\x89PNG\r\n\x1a\n" or data[:3] == b"\xff\xd8\xff") and len(data) <= MAX_BYTES:
        return []
    problems = []
    low = path.lower()
    for d in DIRS:
        if low.startswith(d) or ("/" + d) in low:
            problems.append("game-data/build directory")
            break
    for e in EXTS:
        if low.endswith(e):
            problems.append("forbidden file type (%s)" % e)
            break
    if len(data) > MAX_BYTES:
        problems.append("%d bytes > %d (possible game data)" % (len(data), MAX_BYTES))
    sha = hashlib.sha1(data).hexdigest()
    if sha in GAME_SHA1:
        problems.append("this is %s" % GAME_SHA1[sha])
    if b"\0" in data:
        problems.append("binary file")
        return problems
    text = data.decode("utf-8", "replace")
    if len(ASM_LINE.findall(text)) > 5:
        problems.append("contains disassembly")
    if len(RECOMP_LINE.findall(text)) > 20:
        problems.append("contains recompiled game code")
    if SECRET.search(text):
        problems.append("contains a key = hex secret")
    for m in HOME_PATH.finditer(text):
        if not HOME_OK.search(m.group(0)):
            problems.append("contains a personal path (%s)" % m.group(0))
            break
    return problems


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def git(*args):
    return subprocess.run(("git",) + args, capture_output=True, check=True).stdout


def main():
    # a refused path with letters the console can't show must not crash the check (and so pass it)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    mode = sys.argv[1] if len(sys.argv) > 1 else "--tree"
    bad = 0
    if mode == "--staged":
        names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").decode().split("\0")
        items = [(n, lambda n=n: git("show", ":" + n)) for n in names if n]
    elif mode == "--history":
        seen = set()
        items = []
        for line in git("rev-list", "--all", "--objects").decode().splitlines():
            parts = line.split(" ", 1)
            if len(parts) != 2 or parts[0] in seen:
                continue
            seen.add(parts[0])
            if git("cat-file", "-t", parts[0]).strip() != b"blob":
                continue
            items.append((parts[1], lambda o=parts[0]: git("cat-file", "blob", o)))
    else:
        names = git("ls-files", "-z").decode().split("\0")
        items = [(n, lambda n=n: read_file(n)) for n in names if n and os.path.isfile(n)]
    for name, get in items:
        if name.replace("\\", "/") == "tools/check_no_game_data.py":
            continue
        for p in check(name, get()):
            print("refused: %s: %s" % (name, p))
            bad += 1
    if bad:
        print("check_no_game_data: %d problem(s). Game data, builds, keys, logs and personal paths never go in git." % bad)
        return 1
    print("check_no_game_data: %d file(s) checked, nothing game-derived or private found" % len(items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
