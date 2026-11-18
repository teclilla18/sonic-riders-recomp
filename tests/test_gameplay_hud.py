"""Compile focused tests of the actual runtime (no disc data needed).

Usage: python tests/test_gameplay_hud.py --cc <clang-or-gcc>
On Windows use the host's llvm-mingw compiler; Linux/macOS may use clang or gcc.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    args = ap.parse_args()
    source = (ROOT / 'runtime/src/gx_render.c').read_text()
    def section(start, end):
        return source[source.index(start):source.index(end, source.index(start))]
    # Extract verbatim so tests cannot silently drift into a second implementation. Including the
    # complete renderer would require unrelated platform/device link dependencies on Windows.
    helpers = ('#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
               '#include "gx_render.h"\n#include "runtime.h"\n#include "game_addrs.h"\n#include "settings.h"\n'
               'static unsigned long long g_frames;\n')
    helpers += section('static void* grow(', '/* host time')
    helpers += section('static float fbits(', '/* the viewport of')
    helpers += section('static void ov_bbox(', '/* Local split-screen HUD.')
    helpers += section('/* Local split-screen HUD.', 'static void ownview_frame(')
    out = ROOT / 'build/tests'
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        tmp = Path(tmp)
        (tmp / 'local_hud_under_test.inc').write_text(helpers)
        for test in ('game_levels', 'local_hud'):
            exe = tmp / (test + ('.exe' if os.name == 'nt' else ''))
            cmd = [args.cc, '-std=gnu11', '-O1', '-I' + str(tmp),
                   '-I' + str(ROOT / 'runtime/include'), '-I' + str(ROOT / 'runtime/src')]
            if os.name == 'nt':
                cmd += ['-I' + str(ROOT / 'runtime/include/compat')]
            cmd += [str(ROOT / f'tests/test_{test}.c'), '-o', str(exe)]
            if os.name != 'nt':
                cmd += ['-lm']
            subprocess.run(cmd, check=True, cwd=ROOT)
            subprocess.run([str(exe)], check=True, cwd=ROOT)


if __name__ == '__main__':
    main()
