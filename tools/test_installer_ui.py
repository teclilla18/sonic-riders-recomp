"""Off-screen Windows installer regression matrix; never builds or installs game content.

Run after tools/build_launcher.py. Uses a disposable source-free fixture, checks
real maximize/restore/WM_DPICHANGED transitions and records the app's own PNGs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SIZES = [(1280, 780, 96), (900, 620, 96), (1366, 768, 96), (1920, 1080, 96),
         (2560, 1440, 144), (3840, 2160, 192), (3440, 1440, 120), (5120, 1440, 96),
         (1720, 1120, 192), (1920, 1440, 240), (1440, 1000, 168)]
OUTPUT = {"pc": r"build\host-mingw", "switch": r"build\switch", "android": r"build\android",
          "switch2": r"build\switch2\sd\switch\sonicriders-switch2"}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT / "build/launcher/SonicRidersLauncher.exe")
    ap.add_argument("--modes", default="pc,switch,android")
    ap.add_argument("--out", type=Path, default=ROOT / "build/installer-ui")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix="sr installer UI ") as temporary:
        fixture = Path(temporary)
        (fixture / "setup").mkdir()
        (fixture / "setup/setup.ps1").write_text("# UI fixture; never executed\n")
        (fixture / "launcher").mkdir()
        shutil.copyfile(ROOT / "launcher/banner.jpg", fixture / "launcher/banner.jpg")
        exe = fixture / "SonicRidersLauncher.exe"
        shutil.copyfile(a.exe, exe)
        sentinels = {"saves/settings.ini": b"# preserve settings\nfov=115\n", "saves/card_a.raw": b"save sentinel",
                     "mods/mods.ini": b"# user mod choices\n", "vk_pipeline_cache.bin": b"cache sentinel"}
        for name, data in sentinels.items():
            p = fixture / name
            p.parent.mkdir(exist_ok=True, parents=True)
            p.write_bytes(data)
        for mode in a.modes.split(","):
            for width, height, dpi in SIZES:
                for state in range(4):
                    name = f"{mode}-{width}x{height}-{dpi}-state{state}"
                    shot = (a.out / (name + ".png")).resolve()
                    env = os.environ.copy()
                    for k in ["SR_LAUNCHER_INSTALL", "SR_LAUNCHER_TEST", "SR_LAUNCHER_UPDATE_TEST", "SR_LAUNCHER_ADDPACK"]:
                        env.pop(k, None)
                    env.update(SR_INSTALLER=mode, SR_LAUNCHER_SHOT=str(shot),
                               SR_LAUNCHER_UI_TEST=f"{width}x{height}@{dpi}:{state}")
                    output = fixture / OUTPUT[mode].replace("\\", "/")
                    output.mkdir(parents=True, exist_ok=True)
                    if state == 2 and (width, height, dpi) == SIZES[0]:
                        env["SR_LAUNCHER_UI_OPEN"] = "1"
                    p = subprocess.run([str(exe)], env=env, cwd=fixture, timeout=45, capture_output=True)
                    assert p.returncode == 0, (name, p.returncode, p.stderr[-400:])
                    if "SR_LAUNCHER_UI_OPEN" in env:
                        opened = Path(str(shot) + ".open.txt").read_text()
                        assert opened == "PASS " + str(output), (name, opened)
                    png = shot.read_bytes()
                    assert png[:8] == b"\x89PNG\r\n\x1a\n"
                    assert struct.unpack(">II", png[16:24]) == (width, height), name
                    report = Path(str(shot) + ".txt").read_text()
                    match = re.fullmatch(r"client=(\d+)x(\d+) backbuffer=(\d+)x(\d+) dpi=([0-9.]+) state=(\d+) columns=([12]) output=(.+)\n", report)
                    assert match, report
                    cw, ch, bw, bh, scale, st, cols, output = match.groups()
                    assert (int(cw), int(ch), int(bw), int(bh)) == (width, height, width, height), (name, report)
                    assert abs(float(scale) - dpi / 96) < 0.006 and int(st) == state, (name, report)
                    assert output == OUTPUT[mode], (name, output)
                    results.append({"case": name, "report": report.strip(), "sha256": hashlib.sha256(png).hexdigest()})
            print(f"{mode}: {len(SIZES) * 4} resize/DPI/state cases passed", flush=True)
        cleanup = r"""
param([string]$fixture)
$shell = New-Object -ComObject Shell.Application
foreach ($window in @($shell.Windows())) {
    try {
        $folder = $window.Document.Folder.Self.Path
        if ($folder.StartsWith($fixture + '\', [StringComparison]::OrdinalIgnoreCase)) { $window.Quit() }
    } catch {}
}
"""
        script = fixture / "close-test-folders.ps1"
        script.write_text(cleanup)
        subprocess.run(["powershell.exe", "-NoProfile", "-File", str(script), str(fixture)], check=True, capture_output=True)
        for name, data in sentinels.items():
            assert (fixture / name).read_bytes() == data, "User file changed: " + name
        assert not (fixture / "launcher_test.txt").exists(), "Unexpected install execution"
    report = {"result": "PASS", "cases": len(results), "user_files_preserved": True, "output_button_shell_open": True,
              "dpi_percent": [100, 125, 150, 175, 200, 250], "results": results}
    (a.out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(results)} cases; user saves/settings/mods/cache sentinels unchanged")


if __name__ == "__main__":
    main()
