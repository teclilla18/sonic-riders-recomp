#!/usr/bin/env python3
"""Clean-install test of a release zip in Windows Sandbox (a fresh Windows with nothing installed).

  python tools/sandbox_test.py --flavor pc      [--zip build/release/SonicRiders-PC-Setup-v0.4.1.zip]
  python tools/sandbox_test.py --flavor android [--space] [--cache <downloads folder>] [--launch]

Writes build/sandbox/<run>/ (the .wsb and the logon script; gitignored, the .wsb names the folder of
your disc image) and an empty build/sandbox/results/<run>/, the only folder the sandbox can write to.
Inside the sandbox the logon script, like a new user:
  - copies the zip to Downloads and marks it as downloaded from the internet (Zone.Identifier ZoneId=3),
  - extracts it with the Explorer shell (so the mark-of-the-web reaches the .bat like after "Extract All"),
  - runs "Install Sonic Riders (<Plat>).bat" with the unattended flags (README-FIRST's step), or with
    --launcher (pc) SonicRidersLauncher.exe with SR_LAUNCHER_INSTALL (its INSTALL AND PLAY; exit code 0 =
    installed and PLAY ready; its launcher_test.txt is copied to the results too),
  - PASS = setup exit code 0 and the .exe / .apk exists; RESULT.txt is always written (also on a crash),
  - copies only setup.log, progress.txt and RESULT.txt to the results folder (never the image or a build).
--space adds a second install in "C:\\Sonic Riders Test" (same sandbox, tools then already downloaded).
--cache maps a folder of setup downloads read-only and seeds the tools folder from it: only for
fix-and-rerun loops; the check before a release runs cold (no --cache).

The disc image: --iso, else a line "sandbox-iso: <path>" in NOTES.local.md, else baserom/*.iso. Its
folder is mapped read-only. The sandbox needs the Windows Sandbox feature (Windows 10/11 Pro).
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAT = {"pc": "PC", "android": "Android"}
OUTPUT = {"pc": [r"build\host-mingw\sonicriders.exe"], "android": [r"build\android\sonicriders-arm64-v8a.apk"]}
EXTRA_ARGS = {"pc": ["-NoSd", "-Yes"], "android": ["-NoSd", "-Yes", "-AcceptAndroidSdkLicense", "-Install", "no"]}
SANDBOX_EXE = r"C:\Windows\System32\WindowsSandbox.exe"

# Runs inside the sandbox as WDAGUtilityAccount. C:\sr\run is this run's folder (read-only),
# C:\sr\results the only writable mapping.
LOGON_PS1 = r'''
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$cfg = Get-Content C:\sr\run\config.json -Raw | ConvertFrom-Json
$res = "C:\sr\results"
$progress = Join-Path $res "progress.txt"
function P([string]$m) { $l = "{0:HH:mm:ss} {1}" -f (Get-Date), $m; Add-Content -Path $progress -Value $l; Write-Host $l }
$results = @()
try {
    P "sandbox up; flavor $($cfg.flavor), zip $($cfg.zip)"
    if ($cfg.cache) {
        $dl = Join-Path $env:LOCALAPPDATA "SonicRidersSetup\tools\downloads"
        New-Item -ItemType Directory -Force -Path $dl | Out-Null
        Copy-Item C:\sr\cache\* $dl -Force
        P "seeded the tools downloads from the read-only cache ($((Get-ChildItem $dl).Count) files; NOT a cold run)"
    }
    # 1. "download": copy the zip and mark it as coming from the internet, like a browser does
    $dlDir = Join-Path $env:USERPROFILE "Downloads"
    New-Item -ItemType Directory -Force -Path $dlDir | Out-Null
    $zip = Join-Path $dlDir $cfg.zip
    Copy-Item (Join-Path C:\sr\release $cfg.zip) $zip
    Set-Content -Path $zip -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3`r`nHostUrl=https://github.com/`r`n"
    P "zip copied and marked (ZoneId=3)"
    $i = 0
    foreach ($dest in $cfg.dests) {
        $i++
        $t0 = Get-Date
        $r = "FAIL"; $why = ""
        try {
            # 2. Extract All, through the Explorer shell (carries the mark to every file)
            New-Item -ItemType Directory -Force -Path $dest | Out-Null
            $shell = New-Object -ComObject Shell.Application
            $shell.NameSpace($dest).CopyHere($shell.NameSpace($zip).Items(), 0x14)
            $top = Join-Path $dest $cfg.top
            $bat = Join-Path $top $cfg.bat
            for ($w = 0; $w -lt 600 -and -not (Test-Path $bat); $w++) { Start-Sleep -Milliseconds 500 }
            $last = -1
            for ($w = 0; $w -lt 120; $w++) {   # CopyHere returns early: wait until the file count is stable
                $n = (Get-ChildItem $top -Recurse -File -ErrorAction SilentlyContinue).Count
                if ($n -eq $last -and $n -ge $cfg.files) { break }
                $last = $n; Start-Sleep -Seconds 1
            }
            if (-not (Test-Path $bat)) { throw "extract: $($cfg.bat) not found in $top" }
            $motw = [bool](Get-Item $bat -Stream Zone.Identifier -ErrorAction SilentlyContinue)
            P "run ${i}: extracted $last of $($cfg.files) files to $top (mark-of-the-web on the .bat: $motw)"
            # 3. the .bat README-FIRST points to, unattended
            $iso = Join-Path C:\sr\iso $cfg.iso
            $env:SR_NOPAUSE = "1"
            $line = '"' + $bat + '" -Iso "' + $iso + '" ' + ($cfg.args -join " ") + ' <nul'
            P "run ${i}: cmd /c $line"
            $logJob = Start-Job -ArgumentList (Join-Path $top "setup.log"), (Join-Path $res "setup-$i.log") -ScriptBlock {
                param($src, $dst)
                while ($true) { Start-Sleep -Seconds 30; try { Copy-Item $src $dst -Force } catch {} }
            }
            if ($cfg.launcher) {
                # README-FIRST's first way: the launcher's "Select disc..." + INSTALL AND PLAY (SR_LAUNCHER_INSTALL)
                $exe = Join-Path $top "SonicRidersLauncher.exe"
                $motw = [bool](Get-Item $exe -Stream Zone.Identifier -ErrorAction SilentlyContinue)
                P "run ${i}: launcher install instead (mark-of-the-web on the exe: $motw)"
                $env:SR_LAUNCHER_INSTALL = $iso
                $p = [Diagnostics.Process]::Start((New-Object Diagnostics.ProcessStartInfo -Property @{
                    FileName = $exe; WorkingDirectory = $top; UseShellExecute = $false }))
                $env:SR_LAUNCHER_INSTALL = $null
            } else {
                # wait for cmd itself: Start-Process -Wait also waits for children left running (the adb server)
                $p = [Diagnostics.Process]::Start((New-Object Diagnostics.ProcessStartInfo -Property @{
                    FileName = "cmd.exe"; Arguments = "/d /s /c `"$line`""; UseShellExecute = $false }))
            }
            $p.WaitForExit()
            Stop-Job $logJob; Remove-Job $logJob -Force
            $code = $p.ExitCode
            $rep = Join-Path $top "launcher_test.txt"
            if (Test-Path $rep) { Copy-Item $rep (Join-Path $res "launcher_test-$i.txt") -Force }
            $missing = @($cfg.outputs | Where-Object { -not (Test-Path (Join-Path $top $_)) })
            if ($code -ne 0) { $why = "setup exit code $code" }
            elseif ($missing.Count) { $why = "exit code 0 but missing: $($missing -join ', ')" }
            else { $r = "PASS"; $why = "exit code 0, " + ($cfg.outputs -join ", ") + " present" }
        } catch {
            $why = "script error: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))"
        } finally {
            $log = Join-Path (Join-Path $dest $cfg.top) "setup.log"
            if (Test-Path $log) { Copy-Item $log (Join-Path $res "setup-$i.log") -Force }
            $mins = [int]((Get-Date) - $t0).TotalMinutes
            $results += "run ${i} ${r}: $dest, $mins min, $why"
            P "run ${i} ${r}: $why"
        }
    }
} catch {
    $results += "run FAIL: script error: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))"
} finally {
    if (-not $results.Count) { $results += "run FAIL: no run finished" }
    $all = if (@($results | Where-Object { $_ -notmatch "^run \d+ PASS" }).Count) { "FAIL" } else { "PASS" }
    Set-Content -Path (Join-Path $res "RESULT.txt") -Value (@($all) + $results)
    P "RESULT $all"
    if ($cfg.shutdown) { P "closing the sandbox"; Stop-Computer -Force }
}
'''


MINIMIZE_PS1 = r'''
Add-Type -Name W -Namespace SR -MemberDefinition '[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);'
Start-Process "C:\Windows\System32\WindowsSandbox.exe" -ArgumentList ('"' + '@WSB@' + '"') -WindowStyle Minimized
$min = 0; $pri = 0
for ($i = 0; $i -lt 60; $i++) {   # the client window can be re-created while the sandbox boots
    Start-Sleep -Seconds 1
    foreach ($p in Get-Process WindowsSandbox* -ErrorAction SilentlyContinue) {
        if ($p.MainWindowHandle -ne 0) { [void][SR.W]::ShowWindow($p.MainWindowHandle, 7); $min++ }  # SW_SHOWMINNOACTIVE
    }
    foreach ($p in Get-Process vmmem* -ErrorAction SilentlyContinue) {
        try { $p.PriorityClass = "BelowNormal"; $pri++ } catch {}
    }
}
"window minimized: $($min -gt 0); VM priority below normal: $($pri -gt 0)"
'''


def notes_iso():
    path = os.path.join(ROOT, "NOTES.local.md")
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = re.match(r"\s*[-*]?\s*sandbox-iso:\s*(.+?)\s*$", line)
                if m:
                    return m.group(1).strip("`\"'")
    hits = glob.glob(os.path.join(ROOT, "baserom", "*.iso"))
    return hits[0] if hits else None


def xml(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def mapping(host, inside, ro):
    return ("    <MappedFolder><HostFolder>%s</HostFolder><SandboxFolder>%s</SandboxFolder>"
            "<ReadOnly>%s</ReadOnly></MappedFolder>" % (xml(host), xml(inside), "true" if ro else "false"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--flavor", choices=sorted(PLAT), required=True)
    ap.add_argument("--zip", help="release zip (default: the newest build/release/SonicRiders-<Plat>-Setup-*.zip)")
    ap.add_argument("--iso", help="disc image (default: NOTES.local.md 'sandbox-iso:', else baserom/*.iso)")
    ap.add_argument("--space", action="store_true", help=r'second install in "C:\Sonic Riders Test"')
    ap.add_argument("--cache", help="folder of setup downloads, mapped read-only (fix-and-rerun loops only)")
    ap.add_argument("--memory", type=int, default=8192, help="sandbox RAM in MB (default 8192)")
    ap.add_argument("--keep-open", action="store_true", help="leave the sandbox running after the result")
    ap.add_argument("--launch", action="store_true", help="start Windows Sandbox with the new .wsb")
    ap.add_argument("--launcher", action="store_true", help="pc: install with SonicRidersLauncher.exe "
                    "(SR_LAUNCHER_INSTALL) instead of the .bat; also checks its ready check")
    ap.add_argument("--run", help="run name (default <flavor>-<timestamp>)")
    a = ap.parse_args()

    if not os.path.exists(SANDBOX_EXE):
        sys.exit("Windows Sandbox is not installed (%s); turn on the 'Windows Sandbox' feature." % SANDBOX_EXE)
    plat = PLAT[a.flavor]
    zp = a.zip
    if not zp:
        hits = glob.glob(os.path.join(ROOT, "build", "release", "SonicRiders-%s-Setup-*.zip" % plat))
        if not hits:
            sys.exit("no release zip: run python tools/make_release.py --flavor %s --version <v>" % a.flavor)
        zp = max(hits, key=os.path.getmtime)
    zp = os.path.abspath(zp)
    iso = a.iso or notes_iso()
    if not iso or not os.path.isfile(iso):
        sys.exit("disc image not found (pass --iso or add 'sandbox-iso: <path>' to NOTES.local.md)")
    iso = os.path.abspath(iso)

    import zipfile
    with zipfile.ZipFile(zp) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
    top = names[0].split("/")[0]
    bat = "Install Sonic Riders (%s).bat" % plat
    if "%s/%s" % (top, bat) not in names:  # Switch / Android zips: the GUI installer .exe + this console fallback
        bat = "Install Sonic Riders (%s) - command line.bat" % plat
    if "%s/%s" % (top, bat) not in names:
        sys.exit("%s has no %s" % (zp, bat))

    run = a.run or "%s-%s" % (a.flavor, time.strftime("%Y%m%d-%H%M%S"))
    rundir = os.path.join(ROOT, "build", "sandbox", run)
    resdir = os.path.join(ROOT, "build", "sandbox", "results", run)
    if os.path.exists(resdir) and os.listdir(resdir):
        sys.exit("results folder not empty: %s" % resdir)
    os.makedirs(rundir, exist_ok=True)
    os.makedirs(resdir, exist_ok=True)

    dests = [r"C:\SonicRiders"] + ([r"C:\Sonic Riders Test"] if a.space else [])
    cfg = {"flavor": a.flavor, "zip": os.path.basename(zp), "top": top, "bat": bat, "files": len(names),
           "iso": os.path.basename(iso), "args": EXTRA_ARGS[a.flavor], "outputs": OUTPUT[a.flavor],
           "dests": dests, "cache": bool(a.cache), "shutdown": not a.keep_open, "launcher": a.launcher}
    with open(os.path.join(rundir, "config.json"), "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=1)
    with open(os.path.join(rundir, "sandbox_logon.ps1"), "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write(LOGON_PS1.lstrip("\n"))

    maps = [mapping(os.path.dirname(iso), r"C:\sr\iso", True),
            mapping(os.path.dirname(zp), r"C:\sr\release", True),
            mapping(rundir, r"C:\sr\run", True),
            mapping(resdir, r"C:\sr\results", False)]
    if a.cache:
        maps.append(mapping(os.path.abspath(a.cache), r"C:\sr\cache", True))
    wsb = "\n".join([
        "<Configuration>",
        "  <MemoryInMB>%d</MemoryInMB>" % a.memory,
        "  <Networking>Enable</Networking>",
        "  <vGPU>Disable</vGPU>",
        "  <ClipboardRedirection>Disable</ClipboardRedirection>",
        "  <MappedFolders>"] + maps + [
        "  </MappedFolders>",
        "  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File "
        "C:\\sr\\run\\sandbox_logon.ps1</Command></LogonCommand>",
        "</Configuration>", ""])
    wsbpath = os.path.join(rundir, "sonicriders-%s.wsb" % run)
    with open(wsbpath, "w", encoding="utf-8") as f:
        f.write(wsb)
    print("sandbox_test: %s" % wsbpath)
    print("  zip %s (%d files)%s%s" % (os.path.basename(zp), len(names), ", + space path" if a.space else "",
                                       ", WARM (cache)" if a.cache else ", cold"))
    print("  results: %s (RESULT.txt, progress.txt, setup-N.log)" % resdir)
    if a.launch:
        # minimized without focus, the VM below normal priority: the PC stays usable during the run
        ps = MINIMIZE_PS1.replace("@WSB@", wsbpath.replace("'", "''"))
        out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True)
        print("  launched Windows Sandbox: %s" % (out.stdout.strip() or out.stderr.strip()[:200]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
