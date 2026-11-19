#!/usr/bin/env python3
"""Test this app's real process exit (never force-stop to report a successful quit).

Menu mode taps the port's Quit Game + confirmation in its 1280x720 overlay coordinate system.
Perftest mode waits for the natural end of the two-demo route. Logs/exit records stay local.
The fresh PID must have Android EXIT_SELF/status0, no fatal signal, and optionally complete
shutdown breadcrumbs. A fresh in-memory performance-test card preserves the player's save.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET


def exit_record(text, pid):
    """Match the tested PID, not a stale crash/force-stop from another process incarnation."""
    for record in re.split(r'ApplicationExitInfo\s*#\d+\s*:', text):
        match = re.search(r'\bpid\s*=\s*(\d+)\b', record)
        if not match or match.group(1) != str(pid):
            continue
        reason = re.search(r'\breason\s*=\s*(\d+)', record)
        status = re.search(r'\bstatus\s*=\s*(\d+)', record)
        if reason and status:
            return {'reason': int(reason.group(1)), 'status': int(status.group(1))}
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True)
    ap.add_argument('--args', default='')
    ap.add_argument('--serial', default=os.environ.get('ANDROID_SERIAL'))
    ap.add_argument('--package', default='net.srnative.sonicriders')
    ap.add_argument('--trigger', choices=['menu', 'perftest'], default='menu')
    ap.add_argument('--timeout', type=float)
    ap.add_argument('--require-cleanup', action='store_true')
    ap.add_argument('--background-resume', action='store_true', help='Home/resume before menu quit')
    a = ap.parse_args()
    if a.background_resume and a.trigger != 'menu': ap.error('--background-resume requires --trigger menu')
    if not a.serial: ap.error('--serial (or ANDROID_SERIAL) is required')
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('LOCALAPPDATA', Path.home()))/'SonicRidersSetup/tools/android-sdk/platform-tools/adb.exe'
    adb = [str(sdk) if sdk.is_file() else 'adb', '-s', a.serial]
    pkg = a.package

    def sh(*args, check=True):
        r = subprocess.run(adb+list(args), capture_output=True, text=True, encoding='utf-8', errors='replace')
        if check and r.returncode: raise RuntimeError((args, r.stderr, r.stdout))
        return r.stdout.strip()

    sh('shell', 'am', 'force-stop', pkg)  # setup only; never counted as the tested exit
    (out/'exit-before.txt').write_text(sh('shell', 'dumpsys', 'activity', 'exit-info', pkg, check=False))
    since = sh('shell', 'date', '+%m-%dT%H:%M:%S.000').replace('T', ' ')
    args = 'SR_PERFTEST=1 SR_NOUPDATECHECK=1 '+('SR_TEST_MENU=0 ' if a.trigger == 'menu' else '')+a.args
    sh('shell', 'am', 'start', '-W', '-n', pkg+'/net.srnative.sonicriders.SonicRidersActivity', '--es', 'args', "'"+args.replace("'", '')+"'")
    pid = ''
    for _ in range(50):
        pid = sh('shell', 'pidof', pkg, check=False)
        if pid: break
        time.sleep(.1)
    if not pid or ' ' in pid: raise RuntimeError('exactly one app PID is required')
    with (out/'logcat.txt').open('w', encoding='utf-8') as log:
        proc = subprocess.Popen(adb+['logcat', '-v', 'threadtime', '-T', since, '--pid='+pid], stdout=log, stderr=subprocess.STDOUT)
        try:
            if a.trigger == 'menu':
                time.sleep(3)
                # Preserve a previous test's crash report, then dismiss only this app's native
                # "Close" dialog. It must not intercept the port's actual Quit Game taps.
                sh('shell', 'uiautomator', 'dump', '/data/local/tmp/sr-quit-ui.xml', check=False)
                xml = sh('shell', 'cat', '/data/local/tmp/sr-quit-ui.xml', check=False)
                try:
                    for node in ET.fromstring(xml).iter('node'):
                        if (node.get('package') == pkg and node.get('resource-id') == 'android:id/button2'
                                and node.get('text') == 'Close'):
                            bounds = list(map(int, re.findall(r'\d+', node.get('bounds', ''))))
                            if len(bounds) == 4:
                                sh('shell', 'input', 'tap', str((bounds[0]+bounds[2])//2), str((bounds[1]+bounds[3])//2))
                                (out/'startup-report-dismissed.txt').write_text('Dismissed previous-run report; not the tested exit.\n')
                                time.sleep(.5)
                except ET.ParseError:
                    pass
                if a.background_resume:
                    sh('shell', 'input', 'keyevent', 'KEYCODE_HOME')
                    time.sleep(2)
                    sh('shell', 'am', 'start', '-W', '-n', pkg+'/net.srnative.sonicriders.SonicRidersActivity')
                    time.sleep(2)
                # The app locks landscape. Preserve its overlay aspect when computing tap locations.
                sizes = re.findall(r'(\d+)x(\d+)', sh('shell', 'wm', 'size'))
                width, height = sorted(map(int, sizes[-1]), reverse=True)
                scale = min(width/1280, height/720)
                ox, oy = (width-1280*scale)/2, (height-720*scale)/2
                for x, y in ((1055, 645), (753, 400)):
                    sh('shell', 'input', 'tap', str(round(ox+x*scale)), str(round(oy+y*scale)))
                    time.sleep(.8)
            deadline = time.monotonic()+(a.timeout or (320 if a.trigger == 'perftest' else 30))
            while time.monotonic() < deadline and sh('shell', 'pidof', pkg, check=False): time.sleep(.2)
            exited = not sh('shell', 'pidof', pkg, check=False)
            record, after = None, ''
            for _ in range(30):
                after = sh('shell', 'dumpsys', 'activity', 'exit-info', pkg, check=False)
                record = exit_record(after, pid)
                if record or not exited: break
                time.sleep(.2)
        finally:
            proc.terminate(); proc.wait()
    full = sh('logcat', '-d', '-v', 'threadtime', '-T', since, '--pid='+pid, check=False)
    if full: (out/'logcat.txt').write_text(full)
    (out/'exit-after.txt').write_text(after)
    for name in ('sonicriders.log', 'crash.txt'):
        subprocess.run(adb+['pull', f'/sdcard/Android/data/{pkg}/files/{name}', str(out/name)], capture_output=True)
    # debuggerd has its own PID. Retain only the emitter associated with this app's fresh PID.
    crash = sh('logcat', '-d', '-b', 'crash', '-v', 'threadtime', '-s', 'DEBUG', check=False).splitlines()
    emitters = {line.split()[2] for line in crash if re.search(r'pid:\s*'+pid+r'\b', line) and len(line.split()) > 4}
    own = [line for line in crash if len(line.split()) > 4 and line.split()[2] in emitters]
    if own: (out/'debuggerd.txt').write_text('\n'.join(own)+'\n')
    fatal = bool(re.search(r'\[crash\] (?:SIG|signal)|Fatal signal|FATAL EXCEPTION|FATAL:|FORTIFY:', full))
    cleanup = ('[shutdown] SDL stopped; shutdown complete' in full
               and '[shutdown] application exit handlers complete; terminating status 0' in full)
    passed = exited and record == {'reason': 1, 'status': 0} and not fatal and (cleanup or not a.require_cleanup)
    result = {'pid': pid, 'exited': exited, 'exit_record': record, 'fatal': fatal,
              'cleanup_complete': cleanup, 'passed': passed, 'trigger': a.trigger,
              'background_resume': a.background_resume, 'args': a.args}
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    return 0 if passed else 1


if __name__ == '__main__': raise SystemExit(main())
