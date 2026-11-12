#!/usr/bin/env python3
"""Sonic Riders setup for Linux and the Steam Deck (started by SonicRidersSetup.sh).

    python3 tools/setup_linux.py [--iso IMAGE] [--yes] [--tools DIR] [--compiler auto|system|zig]
                                 [--desktop-shortcut yes|no]

The same steps as the Windows setup (setup/setup.ps1), for the PC version only:
  1. check your disc image (Sonic Riders USA, GXEE8P revision 0, SHA-1 of the Redump dump)
  2. download the tools, each checked by SHA-256 (setup/toolchain.json "linux" + "imgui"):
     decomp-toolkit, ninja, Dear ImGui, and only when the system lacks them: SDL2's headers and
     the zig C/C++ compiler (SteamOS has no compiler and a read-only system, so nothing is installed
     into the system and no root rights are needed)
  3. extract main.dol and _Main.rel from the image
  4. analyse the game code (decomp-toolkit)
  5. recompile and build (configure.py --toolchain linux, ninja)
  6. a launcher ("Sonic Riders.sh"), a menu entry, and how to add it to Steam
Everything stays in this folder and the tools folder (default ~/.local/share/SonicRidersSetup/tools).
Standard library only. Nothing from the game leaves this machine.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO_SHA1 = "ce0cecf57034bef8bab7d6f588ce92c85a849613"
LOG = os.path.join(ROOT, "setup.log")
STEPS = 6


def log(msg="", color=""):
    codes = {"green": "32", "yellow": "33", "red": "31", "cyan": "36"}
    if color and sys.stdout.isatty():
        print("\033[%sm%s\033[0m" % (codes[color], msg), flush=True)
    else:
        print(msg, flush=True)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(msg + "\n")


def step(n, msg):
    log()
    log("== %d/%d  %s" % (n, STEPS, msg), "cyan")


def fail(msg, hint=""):
    log()
    log("SETUP FAILED: " + msg, "red")
    if hint:
        log(hint, "yellow")
    log("The whole log is in setup.log in this folder; attach it if you report a problem.")
    sys.exit(1)


def ask(question, default, assume_yes):
    if assume_yes or not sys.stdin.isatty():
        return default
    try:
        a = input("%s [%s] " % (question, default)).strip()
    except EOFError:
        return default
    return a or default


def run(what, cmd, cwd=ROOT, env=None):
    """Run a command; its output goes to the screen and setup.log."""
    log("> " + what)
    with open(LOG, "a", encoding="utf-8") as f:
        f.write("  $ %s\n" % " ".join(cmd))
    e = dict(os.environ, **(env or {}))
    p = subprocess.Popen(cmd, cwd=cwd, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    tail = []
    with open(LOG, "a", encoding="utf-8") as f:
        for line in p.stdout:
            f.write("  " + line)
            tail = (tail + [line.rstrip()])[-30:]
            if line.startswith("[") or "error" in line.lower() or "RECOMP" in line:
                print("  " + line.rstrip()[:160], flush=True)
    if p.wait() != 0:
        log("\n".join("  " + t for t in tail[-12:]))
        fail("%s failed (exit code %d)." % (what, p.returncode))


# ---------------------------------------------------------------- disc image
def pick_iso():
    """A file dialog where there is one (KDE on the Steam Deck: kdialog; GNOME: zenity), else a typed path."""
    title = "Select your Sonic Riders (USA) disc image (GXEE8P, revision 0)"
    home = os.path.expanduser("~")
    gui = os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")
    for cmd in (["kdialog", "--title", title, "--getopenfilename", home, "*.iso *.gcm|GameCube disc image"],
                ["zenity", "--file-selection", "--title=" + title, "--file-filter=GameCube disc image | *.iso *.gcm"]):
        if gui and shutil.which(cmd[0]):
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode == 0 and r.stdout.strip():
                return r.stdout.strip()
    if sys.stdin.isatty():
        p = input("Path of your disc image (.iso; you can drag the file into this window): ").strip()
        return p.strip("'\"")
    return ""


def check_iso(iso):
    if not os.path.isfile(iso):
        fail("Disc image not found: %s" % iso)
    size = os.path.getsize(iso)
    log("Image: %s (%s bytes)" % (iso, format(size, ",")))
    with open(iso, "rb") as f:
        hdr = f.read(8)
    disc_id = hdr[:6].decode("ascii", "replace")
    if disc_id != "GXEE8P":
        fail("This image's game ID is '%s'; Sonic Riders (USA) is GXEE8P." % disc_id,
             "Only the USA release is supported (PAL GXEP8P and Japanese GXEJ8P are not).")
    if hdr[7] != 0:
        fail("This is revision %d of the disc; revision 0 is supported." % hdr[7])
    log("Checking the image's SHA-1 (about 10-30 seconds)...")
    h = hashlib.sha1()
    with open(iso, "rb") as f:
        for chunk in iter(lambda: f.read(8 << 20), b""):
            h.update(chunk)
    log("SHA-1: " + h.hexdigest())
    if h.hexdigest() != ISO_SHA1:
        fail("The image does not match the known-good dump (expected SHA-1 %s)." % ISO_SHA1,
             "A clean 1:1 dump (Redump: 'Sonic Riders (USA) (En,Ja,Fr,De,Es,It)') is needed. Trimmed, compressed "
             "(RVZ/CISO/GCZ) or modified images do not work; convert RVZ back to ISO with Dolphin first.")
    log("Disc image OK: Sonic Riders (USA), GXEE8P revision 0.", "green")
    return size


# ---------------------------------------------------------------- tools
def download(url, sha256, dl_dir):
    os.makedirs(dl_dir, exist_ok=True)
    dest = os.path.join(dl_dir, url.rsplit("/", 1)[1])
    if os.path.isfile(dest) and sha_file(dest) == sha256:
        return dest
    log("Downloading %s" % url)
    tmp = dest + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": "SonicRidersSetup/1.0"})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=60) as r, open(tmp, "wb") as f:
                total = int(r.headers.get("Content-Length") or 0)
                got, last = 0, 0.0
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b)
                    got += len(b)
                    if total and sys.stdout.isatty() and time.time() - last > 0.5:
                        last = time.time()
                        print("\r  %d of %d MB" % (got >> 20, total >> 20), end="", flush=True)
            if sys.stdout.isatty():
                print()
            break
        except OSError as e:
            if attempt == 2:
                fail("Download failed: %s (%s)" % (url, e), "Check the internet connection and run the setup again.")
            log("  retrying (%s)" % e, "yellow")
            time.sleep(3)
    got = sha_file(tmp)
    if got != sha256:
        os.remove(tmp)
        fail("The download of %s does not match its SHA-256 (got %s, expected %s)." % (url, got, sha256),
             "Run the setup again; if it keeps failing, report it.")
    os.replace(tmp, dest)
    return dest


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_tool(spec, tools):
    """Download + unpack one toolchain.json entry into tools/<dir>, once (a marker holds its hash)."""
    d = os.path.join(tools, spec["dir"])
    mark = os.path.join(d, ".setup-ok")
    if os.path.isfile(mark) and open(mark).read().strip() == spec["sha256"]:
        return d
    f = download(spec["url"], spec["sha256"], os.path.join(tools, "downloads"))
    if os.path.isdir(d):
        shutil.rmtree(d)
    stage = d + ".extract"
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(stage)
    log("Extracting %s" % os.path.basename(f))
    if spec["kind"] == "file":
        shutil.copyfile(f, os.path.join(stage, spec["name"]))
    elif spec["kind"] == "zip":
        with zipfile.ZipFile(f) as z:
            for info in z.infolist():
                out = os.path.realpath(os.path.join(stage, info.filename))
                if not out.startswith(os.path.realpath(stage) + os.sep):
                    fail("Unsafe path in %s: %s" % (f, info.filename))
                z.extract(info, stage)
                mode = info.external_attr >> 16
                if mode & 0o111:
                    os.chmod(out, 0o755)
    else:
        with tarfile.open(f) as t:
            if hasattr(tarfile, "data_filter"):
                t.extractall(stage, filter="data")
            else:
                for m in t.getmembers():
                    out = os.path.realpath(os.path.join(stage, m.name))
                    if not out.startswith(os.path.realpath(stage) + os.sep) or m.isdev():
                        fail("Unsafe path in %s: %s" % (f, m.name))
                t.extractall(stage)
    src = os.path.join(stage, spec["strip"]) if spec.get("strip") else stage
    os.rename(src, d)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    for name in os.listdir(d):  # single programs (dtk, ninja) must be executable
        p = os.path.join(d, name)
        if os.path.isfile(p) and spec["kind"] in ("file", "zip") and name in ("dtk", "ninja"):
            os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    with open(mark, "w") as m:
        m.write(spec["sha256"])
    return d


def system_sdl_headers():
    """True when configure.py will find SDL.h itself (same search: sdl2-config, then /usr/include/SDL2)."""
    if os.path.isfile("/usr/include/SDL2/SDL.h"):
        return True
    if shutil.which("sdl2-config"):
        try:
            r = subprocess.run(["sdl2-config", "--cflags"], capture_output=True, text=True, timeout=20)
        except OSError:
            return False
        return any(f.startswith("-I") and os.path.isfile(os.path.join(f[2:], "SDL.h")) for f in r.stdout.split())
    return False


def system_compiler():
    """(cc, cxx) when the system has a working clang or gcc pair (compiles and links C++), else None."""
    for cc, cxx in (("clang", "clang++"), ("gcc", "g++"), ("cc", "c++")):
        if shutil.which(cc) and shutil.which(cxx):
            if compiler_works([cxx]):
                return cc, cxx
    return None


def compiler_works(cxx, tmp_dir=None):
    tmp_dir = tmp_dir or os.path.join(ROOT, "build")
    os.makedirs(tmp_dir, exist_ok=True)
    src = os.path.join(tmp_dir, "cc_test.cpp")
    with open(src, "w") as f:
        f.write("#include <threads.h>\n#include <cstdio>\nint main(){std::printf(\"ok\\n\");return 0;}\n")
    r = subprocess.run(cxx + ["-std=c++20", src, "-o", os.path.join(tmp_dir, "cc_test")], capture_output=True, text=True)
    return r.returncode == 0


# ---------------------------------------------------------------- install
def write_launcher(exe):
    path = os.path.join(ROOT, "Sonic Riders.sh")
    with open(path, "w", newline="\n") as f:
        f.write('#!/bin/sh\n'
                '# Starts the Sonic Riders PC port from this folder (its disc image link, saves/ and settings live here).\n'
                'cd "$(dirname "$0")" || exit 1\n'
                '# from a terminal the log shows there; otherwise (menu, Steam) it goes to sonicriders.log here,\n'
                '# the four runs before it kept as sonicriders.prev.log (the last) .. sonicriders.prev4.log\n'
                '[ -t 2 ] && exec ./{exe} "$@"\n'
                '[ -f sonicriders.prev3.log ] && mv -f sonicriders.prev3.log sonicriders.prev4.log\n'
                '[ -f sonicriders.prev2.log ] && mv -f sonicriders.prev2.log sonicriders.prev3.log\n'
                '[ -f sonicriders.prev.log ] && mv -f sonicriders.prev.log sonicriders.prev2.log\n'
                '[ -f sonicriders.log ] && mv -f sonicriders.log sonicriders.prev.log\n'
                'exec ./{exe} "$@" 2>sonicriders.log\n'.format(exe=os.path.relpath(exe, ROOT)))
    os.chmod(path, 0o755)
    return path


def desktop_entry(launcher, icon):
    def q(p):  # the Exec key quotes arguments with spaces (Desktop Entry spec)
        return '"%s"' % p.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace("`", "\\`")
    lines = ["[Desktop Entry]", "Type=Application", "Name=Sonic Riders",
             "Comment=Sonic Riders (native PC port; your own disc image)",
             "Exec=%s" % q(launcher), "Path=%s" % ROOT, "Terminal=false", "Categories=Game;"]
    if icon:
        lines.append("Icon=%s" % icon)
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Sonic Riders setup for Linux / Steam Deck")
    ap.add_argument("--iso", default="", help="your Sonic Riders (USA) disc image (GXEE8P revision 0, 1.4 GB .iso)")
    ap.add_argument("--yes", action="store_true", help="no questions (needs --iso)")
    ap.add_argument("--tools", default=os.path.join(os.path.expanduser("~"), ".local", "share", "SonicRidersSetup", "tools"),
                    help="where the downloaded tools go (default ~/.local/share/SonicRidersSetup/tools)")
    ap.add_argument("--compiler", choices=["auto", "system", "zig"], default="auto",
                    help="auto: the system's clang/gcc if it works, else download zig (default)")
    ap.add_argument("--desktop-shortcut", choices=["", "yes", "no"], default="", help="also put a shortcut on the desktop")
    a = ap.parse_args()

    with open(LOG, "w", encoding="utf-8") as f:
        f.write("Sonic Riders setup (Linux) %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    log("Sonic Riders setup for Linux / Steam Deck")
    log("System: %s %s, Python %s" % (platform.system(), platform.machine(), platform.python_version()))
    try:
        with open("/etc/os-release") as f:
            name = [l.split("=", 1)[1].strip().strip('"') for l in f if l.startswith("PRETTY_NAME=")]
        if name:
            log("Distribution: %s" % name[0])
    except OSError:
        pass
    if platform.system() != "Linux":
        fail("This setup is for Linux. On Windows use SonicRidersSetup.bat.")
    if not os.path.isfile(os.path.join(ROOT, "configure.py")) or not os.path.isfile(os.path.join(ROOT, "tools", "recomp", "gcrecomp.py")):
        fail("This does not look like the complete release folder (configure.py / tools are missing).",
             "Extract the whole zip first, then run SonicRidersSetup.sh from inside it.")
    if " " in ROOT or not ROOT.isascii():
        log("Note: this folder's path has spaces or non-English letters; if the build fails, move it to e.g. ~/SonicRiders.", "yellow")
    arch = {"amd64": "x86_64", "arm64": "aarch64"}.get(platform.machine().lower(), platform.machine().lower())
    tc = json.load(open(os.path.join(ROOT, "setup", "toolchain.json"), encoding="utf-8"))
    lx = tc["linux"]
    if arch not in lx["dtk"]:
        fail("This CPU (%s) is not supported; x86_64 (Steam Deck, PCs) and aarch64 are." % arch)

    # 1 -------------------------------------------------------------
    step(1, "Your disc image")
    iso = a.iso
    if not iso:
        if a.yes:
            fail("--iso is required with --yes.")
        iso = pick_iso()
        if not iso:
            fail("No disc image selected.", "Run it again and pick the image, or pass it: ./SonicRidersSetup.sh --iso /path/to/image.iso")
    iso = os.path.abspath(os.path.expanduser(iso))
    iso_size = check_iso(iso)

    # 2 -------------------------------------------------------------
    step(2, "Tools (downloaded once, checked by SHA-256)")
    tools = os.path.abspath(os.path.expanduser(a.tools))
    os.makedirs(tools, exist_ok=True)
    log("Tools folder: %s" % tools)
    dtk = os.path.join(ensure_tool(lx["dtk"][arch], tools), "dtk")
    ninja = os.path.join(ensure_tool(lx["ninja"][arch], tools), "ninja")
    imgui = ensure_tool(tc["imgui"], tools)
    conf_args = ["--imgui", imgui]
    if not system_sdl_headers():
        conf_args += ["--sdl", ensure_tool(lx["sdl2_headers"], tools)]
        log("SDL2 headers: downloaded (the game uses the system's SDL2 library).")
    cc = None if a.compiler == "zig" else system_compiler()
    if a.compiler == "system" and not cc:
        fail("No working C/C++ compiler (clang or gcc) found.", "Install one, or run with --compiler auto to download zig.")
    if cc:
        log("Compiler: the system's %s / %s." % cc)
        conf_args += ["--cc", cc[0], "--cxx", cc[1]]
    else:
        zig = os.path.join(ensure_tool(lx["zig"][arch], tools), "zig")
        log("Compiler: zig (%s), no system compiler needed." % os.path.basename(os.path.dirname(zig)))
        # wrappers: ninja runs "$cc ..." through the shell; a path with spaces would split
        wrap = os.path.join(tools, "zig-bin")
        os.makedirs(wrap, exist_ok=True)
        for name, sub in (("zig-cc", "cc"), ("zig-c++", "c++")):
            p = os.path.join(wrap, name)
            with open(p, "w", newline="\n") as f:
                f.write('#!/bin/sh\nexec "%s" %s "$@"\n' % (zig, sub))
            os.chmod(p, 0o755)
        if " " in wrap:
            fail("The tools folder must not contain spaces: %s" % tools, "Pass another one with --tools DIR.")
        conf_args += ["--cc", os.path.join(wrap, "zig-cc"), "--cxx", os.path.join(wrap, "zig-c++")]
        if not compiler_works([os.path.join(wrap, "zig-c++")]):
            fail("The downloaded zig compiler does not work on this system.", "Report it with setup.log attached.")
    log("decomp-toolkit, ninja, Dear ImGui and a compiler ready.", "green")

    # 3 -------------------------------------------------------------
    step(3, "Extracting the game code from your disc image")
    baserom = os.path.join(ROOT, "baserom")
    os.makedirs(baserom, exist_ok=True)
    run("extract main.dol and _Main.rel", [sys.executable, "-I", "tools/extract_iso.py", iso, "baserom", "--no-verify-iso"])
    if not any(n.endswith(".iso") and os.path.getsize(os.path.join(baserom, n)) == iso_size
               for n in os.listdir(baserom) if os.path.exists(os.path.join(baserom, n))):
        dest = os.path.join(baserom, os.path.basename(iso) if iso.endswith(".iso") else "SonicRiders.iso")
        if os.path.lexists(dest):
            os.remove(dest)
        try:
            os.link(iso, dest)
            log("Linked the disc image into baserom/.")
        except OSError:
            os.symlink(iso, dest)
            log("Linked the disc image into baserom/ (a symbolic link: keep the image where it is).")
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    icon = os.path.join(ROOT, "setup", "icon.png")  # the fan-made icon by cedry2kio (README credits)
    if not os.path.isfile(icon):
        log("No program icon (setup/icon.png is missing); the game works without it.", "yellow")
        icon = ""

    # 4 -------------------------------------------------------------
    step(4, "Analysing the game code")
    run("decomp-toolkit analysis", [dtk, "dol", "split", "config/GXEE8P/config.yml", "build/GXEE8P"])

    # 5 -------------------------------------------------------------
    step(5, "Recompiling and building (a few minutes; longer the first time with zig)")
    run("configure", [sys.executable, "configure.py", "--target", "host", "--toolchain", "linux"] + conf_args)
    run("build the PC version", [ninja])
    exe = os.path.join(ROOT, "build", "host-linux", "sonicriders")
    if not os.path.isfile(exe):
        fail("The build finished but %s is missing." % exe)
    log("Built %s" % os.path.relpath(exe, ROOT), "green")

    # 6 -------------------------------------------------------------
    step(6, "Installing")
    launcher = write_launcher(exe)
    log("Created the launcher 'Sonic Riders.sh' in this folder.")
    entry = desktop_entry(launcher, icon)
    apps = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "applications")
    try:
        os.makedirs(apps, exist_ok=True)
        with open(os.path.join(apps, "sonicriders.desktop"), "w") as f:
            f.write(entry)
        log("Added 'Sonic Riders' to the application menu (Games).")
    except OSError as e:
        log("Could not add the menu entry (%s)." % e, "yellow")
    ds = a.desktop_shortcut or ("yes" if ask("Put a 'Sonic Riders' shortcut on your desktop? (y/n)", "y", a.yes).lower().startswith("y") else "no")
    if ds == "yes":
        desk = subprocess.run(["xdg-user-dir", "DESKTOP"], capture_output=True, text=True).stdout.strip() if shutil.which("xdg-user-dir") else ""
        desk = desk or os.path.expanduser("~/Desktop")
        if os.path.isdir(desk):
            p = os.path.join(desk, "sonicriders.desktop")
            with open(p, "w") as f:
                f.write(entry)
            os.chmod(p, 0o755)
            log("Created the desktop shortcut.")
    log()
    log("Done. Start the game with 'Sonic Riders' in the application menu, or ./'Sonic Riders.sh' here.", "green")
    log("Keep this folder: the game, your disc image link and your saves (saves/) live here.", "green")
    log()
    log("Alpha testers needed: the Sonic Heroes native port has its first alpha build.", "yellow")
    log("  https://github.com/teclilla18/sonic-heroes-recomp", "cyan")
    log()
    log("Steam Deck / Steam (Game Mode, controller):")
    log("  1. In Desktop Mode open Steam > Games > Add a Non-Steam Game to My Library.")
    log("  2. Tick 'Sonic Riders' in the list (or Browse to 'Sonic Riders.sh' in this folder) and click Add.")
    log("  3. Back in Game Mode it is in your library under Non-Steam. The Deck's controls work as a game")
    log("     controller (Steam Input); hold the View button (or F1) for the settings menu.")
    log("Keyboard: WASD = move, J = A, K = B, L = X, I = Y, Enter = START, U/O = L/R, P = Z; F11 = fullscreen.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        fail("Stopped.")
