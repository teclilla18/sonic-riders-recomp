#!/usr/bin/env python3
"""Test Android's own-handler/direct-exit path and unchanged non-Android libc semantics.

--android builds an ARM64 PIE; run on device with/without the 'capacity' argument.
Desktop mode also builds the Android code path with __ANDROID__ for deterministic child tests.
"""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    ap.add_argument('--android', action='store_true')
    a = ap.parse_args()
    out = ROOT/'build/tests/runtime-exit'
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'runtime/src'), '-I'+str(ROOT/'runtime/include')]
    if a.android: flags += ['--target=aarch64-linux-android30']
    elif os.name == 'nt': flags += ['-I'+str(ROOT/'runtime/include/compat')]
    else: flags += ['-pthread']
    for android_path in ([True] if a.android else [True, False]):
        exe = out/('android-exit' if android_path else 'host-exit')
        if not a.android and os.name == 'nt': exe = exe.with_suffix('.exe')
        options = flags+(['-D__ANDROID__'] if android_path else [])
        subprocess.run([a.cc, *options, str(ROOT/'runtime/src/shutdown.c'), str(ROOT/'tools/test_runtime_exit.c'), '-o', str(exe)], check=True)
        if a.android:
            print('ARM64 exit regression:', exe)
            continue
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0 and 'buffered:3241' in result.stdout, result
        assert ('process-wide-finalizer' in result.stdout) != android_path, result.stdout
        if android_path:
            result = subprocess.run([str(exe), 'capacity'], capture_output=True, text=True, timeout=10)
            assert result.returncode == 7 and result.stdout.startswith('x'*32), result
            assert 'process-wide-finalizer' not in result.stdout, result.stdout
    print('PASS: application LIFO/reentrant hooks, stream flush, capacity/status, Android skips library finalizers; host retains them')


if __name__ == '__main__': main()
