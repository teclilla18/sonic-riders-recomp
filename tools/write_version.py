"""Write the build id the log prints first (const char g_build_version[]).

configure.py calls this once, and build.ninja runs it before every build so the id follows git without a
re-configure. The file is rewritten only when the id changes, so an unchanged id rebuilds nothing.
Usage: write_version.py <out.c>
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build_version():
    """the VERSION file that tools/make_release.py puts in the release zip, else the git commit"""
    try:
        with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f:
            v = f.read().strip()
            if v:
                return v
    except OSError:
        pass
    try:
        return subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=8"], cwd=ROOT,
                              capture_output=True, text=True, check=True).stdout.strip() or "unknown"
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def write(out):
    path = os.path.join(ROOT, out)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = 'const char g_build_version[] = "%s";\n' % build_version().replace("\\", "").replace('"', "")
    try:
        with open(path, encoding="utf-8") as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


if __name__ == "__main__":
    write(sys.argv[1])
