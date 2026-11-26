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
# picture is refused until its SHA-1 is put here. The temporary art notices are also listed by exact hash.
OWN_ART = {
    "docs/images/v0.9.0/riders-rebuilt-overview.jpg": "367159e2206ec59ffcba9181a18b0c29b865df58",  # Maintainer's Canva artwork using their gameplay capture
    "docs/images/switch2-alpha3-banner.jpg": "ccbd00b191343185ca1d6a2145c82a1ebd9fe09d",  # Maintainer-approved Switch 2 composite with blue background
    "docs/images/v0.8.2/rainbow-rush.jpg": "4ef97573b2fd22e9a5ad24c321dfa982fdda91bd",  # Lanmana artwork supplied and approved by the maintainer for v0.8.2
    "docs/images/sonic-riders-readme.jpg": "f073efe44153a731bc96f6f1f4a3473180487f2c",  # SEGA promotional artwork supplied by the maintainer for the README
    # User-requested documentation: reviewed current installer UI, privacy masks and vector annotations.
    "docs/images/installation/switch-completion.png": "1cb4e8d8313b987baf8fdc731e780fd6b4de6dc0",
    "docs/images/installation/switch-installer.png": "76fe8771aefad69448b6c98767a9e02cb566a664",
    "docs/images/installation/switch-options.png": "d3f833422dd4516892cf3c3d6f0081adcc328ad3",
    "docs/images/installation/switch-select-card.png": "11edb8b1b22b4918e675a8ab53135bda53652b70",
    "docs/images/installation/switch-select-disc.png": "61022475a40e09d810c6109c18162d854e26b1c4",
    "docs/images/installation/switch2-completion.png": "7df010349711695f3d83e43102044fe16a16c4dd",
    "docs/images/installation/switch2-installer.png": "248bd149a28d6219df463c49de49ee75c5f59065",
    "docs/images/installation/switch2-options.png": "38d20665c4661e4bf94671f9de4b58b0d6d2df55",
    "docs/images/installation/switch2-select-card.png": "3415e8d035862c907f3cdeefe8bd3dbe7e84a600",
    "docs/images/installation/switch2-select-disc.png": "eff37dd176652aa70c7c8977d04d0e95f70abfcb",
    "docs/images/switch2-alpha1-banner.jpg": "50f383f932945e183f8a2ab34c8d84d9083e9887",  # Temporary commissioned-art notice
    "docs/images/switch2-poster.jpg": "ccbd00b191343185ca1d6a2145c82a1ebd9fe09d",  # Maintainer-approved Switch 2 composite with blue background
    "docs/images/riders-plus/riders-plus-banner.jpg": "7fa3b8313fe95d24cd5c040a76954685fef24315",  # Temporary commissioned-art notice
    "docs/images/v0.9.0/super-sonic-banner.jpg": "79a2665eb5b64fbce7f0c94920175405cf4e656e",  # Temporary commissioned-art notice
    "launcher/banner.jpg": "0c0d252792e9f873acabec1def46d38675b10f8f",  # Temporary commissioned-art notice
    # README comparison screenshots, original vs this port (the user's OK, 2026-11-11): exactly these four
    "docs/images/compare/original-1.jpg": "4bd5401024ca6f240715dc878434d6d298f59421",
    "docs/images/compare/recomp-1.jpg": "0c6a55e5adb65b61a34af262b847ada0e7df3a8e",
    "docs/images/compare/original-2.jpg": "12b047881bffee3a6474c4d276a37ad03c827d6b",
    "docs/images/compare/recomp-2.jpg": "fcd82ec1a09589caf26a55fcaf501e0f7bd4cdde",
    "docs/images/settings-display.jpg": "c02503fd7d6f88b77049d5b695505cab0fc6bb7b",  # settings menu screenshot for the v0.8.0 notes (the user's OK, 2026-11-11)
    # v0.8.2 release notes (the user's OK, 2026-11-14): the user's ChatGPT banners and a PC race screenshot pair
    "docs/images/v0.8.2/riders-plus-banner.jpg": "50f383f932945e183f8a2ab34c8d84d9083e9887",  # Temporary commissioned-art notice
    "docs/images/issues/mp-level-hud.jpg": "e8d45030f746c1ce4f90e66e622662965dd961e0",  # issue: split-screen HUD stays Lv 3 (user, 2026-11-16)
    "docs/images/v0.8.2/online-coming-soon.jpg": "e4710f7b6b9a3e3f14b827d7dc1e8db5dfa89aac",  # Temporary commissioned-art notice
    "docs/images/v0.8.2/riders-plus-lv5.jpg": "334b4f1100ec2981fd006e6ea71ffbc81046e2e1",
    # README Riders Rebuilt section (the user's OK, 2026-11-16): the user's recorded PC race, replayed at 21:9
    "docs/images/riders-plus/lv4-level-up.jpg": "49e32a6731443637290c05c66091ecf539dfbbd5",
    "docs/images/riders-plus/lv5-level-up.jpg": "e943e0f254e90fc9076ac0dedd5fbbde8db8b0c1",
    "docs/images/riders-plus/gold-capsule-298.jpg": "785f018329aae3140b9fa36fadfc437808b482fb",
    "docs/images/riders-plus/lv5-boost.jpg": "56cefaa56117aa79fc437d676cc7f10fe9287daa",
    "docs/images/riders-plus/trick-rank-ss.jpg": "d8d432b8da8a8a35b25ecaf7131ad59e8620f533",
    "docs/images/riders-plus/tunnel-294.jpg": "8ac53a61c267a2e2cbb103b35d38f4c7b229803d",
    "launcher/icon_256.png": "f9b393122164043da06b4002970f1a78aee9d7af",  # Temporary commissioned-art notice
    "launcher/icon_32.png": "086786f3ba04dfc99c50e9956dd6614db8327640",  # Temporary commissioned-art notice
    "setup/icon.png": "b84f997daba5df2563d0c3526cf633aa87151ebf",  # game icon: cedry2kio's fan-made icon (user's OK, 2026-11-10; README credits)
    "setup/icon_256.jpg": "4ab58b2f4da4418a9febadea57937eae7f2da5a6",  # the same, 256 x 256 for the Switch NRO
    "android/touch_icons/boost_b.png": "e91d748d8a99c76b0aad985ea40ae1437c10e982",
    "android/touch_icons/drift_r.png": "45e1558994abaad8f85966ad7c8ff32ee5cde5ac",
    "android/touch_icons/jump_a.png": "d1ff0d81d64478b89993997059c635d42cd3c331",
    "android/touch_icons/stick_base.png": "7279efbd5b60980cf5af16cb5a1c6666f34a1455",
    "android/touch_icons/stick_knob.png": "181e6e6f4882716c1c743ee5fdd85286224dd6dc",
    "android/touch_icons/tornado_lr.png": "5ab1c2552a4a15757d974abf7aa8e87226a2b441",
}


# Earlier versions of approved pictures, accepted only by --history (they sit in old commits; GitHub Actions scans
# every version ever committed). Fan art the project uses with credit (README credits): the banner (public fan art,
# the user's choice, 2026-11-07) and cedry2kio's game icon (the user's OK, 2026-11-10); and the maintainer's own
# touch-control art. Committing any of these again as a new file is still refused unless it is in OWN_ART.
PAST_ART = {
    "docs/images/switch2-alpha3-banner.jpg": {"3bc7fa019646a0f6b0ad97200748e7a830820fbb"},
    "docs/images/switch2-poster.jpg": {"1b3ad238b2a0864f3b838fbde681415262cc5d47", "557fff627f1130606981565212eb3223e3e86544"},
    "docs/images/installation/switch-installer.png": {"c99dbc9b73cd2eff24ca90320837d46c7323a15d"},
    "docs/images/installation/switch-completion.png": {"117e009d71e381fddb4c4cb6332ece8c87c3f63d"},
    "docs/images/installation/switch2-installer.png": {"2d0427e65a7c73bfdd9d84250ac27995cd60bf60"},
    "docs/images/installation/switch2-completion.png": {"871abff16aa48ed974e3197b4ff43e4ed9fcfb43"},
    "docs/images/riders-plus/riders-plus-banner.jpg": {"db92bc23219a1cc21e32f950d26c205b56a49c11"},
    "docs/images/v0.9.0/super-sonic-banner.jpg": {"66e4eda38e8f110669f51927ebde8a326b82d195"},
    "docs/images/v0.8.2/riders-plus-banner.jpg": {"74a6b9d10125473c5be6130b8520b29aabc5c750"},
    "docs/images/v0.8.2/online-coming-soon.jpg": {"2d581b0f9a375a972d77d26a7d870cfeade37af2"},
    "docs/images/switch2-alpha1-banner.jpg": {"b4be7ce0f7e34e3b56735283781a58c775fc5161"},
    "launcher/icon_256.png": {"80541e60eb82d9440c001da3bc4c7240bda2b111"},
    "launcher/icon_32.png": {"24e3018e7e6651be4cbba8228f81364bf9d0a70e"},
    "docs/images/banner.jpg": {"54c9de3ff1b1c31237fc9649449e1f4bdfae4f9b", "4c0e681fef3ff69d583129825b1023f00a86f0c5"},  # removed from README 2026-11-19
    "launcher/banner.jpg": {"a1d51fb7890668be5dfd126311b946aec8744339"},
    "setup/icon.png": {"b84f997daba5df2563d0c3526cf633aa87151ebf", "b42362f551138986cd53dda64721510c101a80e2"},
    "setup/icon_256.jpg": {"4ab58b2f4da4418a9febadea57937eae7f2da5a6", "849b075587a60443ed7ac41ab4cbf3f61736b4a0"},
    "android/touch_icons/stick_knob.png": {"0c95f1650e2502ae0522a3da1351c9961b19b472"},
}


def check(path, data, past=False):
    if past and hashlib.sha1(data).hexdigest() in PAST_ART.get(path, ()) and (data[:8] == b"\x89PNG\r\n\x1a\n" or data[:3] == b"\xff\xd8\xff") and len(data) <= MAX_BYTES:
        return []
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
        for p in check(name, get(), past=(mode == "--history")):
            print("refused: %s: %s" % (name, p))
            bad += 1
    if bad:
        print("check_no_game_data: %d problem(s). Game data, builds, keys, logs and personal paths never go in git." % bad)
        return 1
    print("check_no_game_data: %d file(s) checked, nothing game-derived or private found" % len(items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
