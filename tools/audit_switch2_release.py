#!/usr/bin/env python3
"""Audit every distributable entry against committed source and the data guard.
The sole generated executable is the existing source-built GUI installer;
it must match the just-built launcher byte-for-byte (never a game binary).
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile
import check_no_game_data as guard

ROOT=Path(__file__).resolve().parents[1]
def git(*args): return subprocess.check_output(['git',*args],cwd=ROOT)
def audit(path):
    commit=git('rev-parse','HEAD').decode().strip()
    assert not git('status','--porcelain','--untracked-files=no').strip(), 'Dirty source checkout'
    tracked=set(filter(None,git('ls-tree','-r','--name-only','-z','HEAD').decode().split('\0')))
    extras={'VERSION','SOURCE_COMMIT','release-metadata.json','README-FIRST.txt',
            'Install Sonic Riders (Switch2).exe','Install Sonic Riders (Switch2) - command line.bat'}
    expected=tracked-{'SonicRidersSetup.bat','SonicRidersSetup.sh'} | extras
    with zipfile.ZipFile(path) as z:
        assert not z.testzip(), 'Corrupt ZIP entry'
        names=z.namelist(); assert len(names)==len({n.lower() for n in names}), 'Duplicate ZIP paths'
        assert len({n.split('/')[0] for n in names})==1, 'Multiple archive roots'
        actual=set()
        for name in names:
            n=name.split('/',1)[1]; actual.add(n)
            p=PurePosixPath(n)
            assert not p.is_absolute() and '..' not in p.parts and '\\' not in n and ':' not in n
            data=z.read(name)
            if n=='Install Sonic Riders (Switch2).exe':
                assert data[:2]==b'MZ' and data==(ROOT/'build/launcher/SonicRidersLauncher.exe').read_bytes()
                # Apply the existing guard's exact personal-path policy. Its
                # public CI placeholder /home/runner is used by llvm-mingw's
                # static libc++abi, and is not a developer's private home path.
                for text in [data.decode('latin1'), data.decode('utf-16le', errors='ignore')]:
                    assert not any(not guard.HOME_OK.search(m.group(0)) for m in guard.HOME_PATH.finditer(text)), 'Private home path embedded in installer'
                continue
            if n!='tools/check_no_game_data.py': assert not guard.check(n,data), (n,guard.check(n,data))
            if n in tracked:
                source=git('show','HEAD:'+n)
                if n.endswith(('.ps1','.bat','.cmd')): source=source.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
                assert data==source, 'Source mismatch: '+n
            if n=='SOURCE_COMMIT': assert data.decode().strip()==commit
            if n=='release-metadata.json':
                meta=json.loads(data); assert meta['source_commit']==commit and meta['higher_fps_included'] and meta['default_fps']==60 and meta['unlimited_experimental']
        assert actual==expected, ('Unexpected/missing entries',actual^expected)
    # Runtime/config/shaders must exactly match the user-selected tested alpha baseline.
    baseline = '80304cace54ad3c138ddf3af288f27578cd6994c'
    assert not git('diff',baseline,'HEAD','--','runtime','config').strip(), 'Runtime/config differs from accepted 0d995f3def88 source'
    checksum=hashlib.sha256(Path(path).read_bytes()).hexdigest()
    assert (Path(str(path)+'.sha256')).read_text().split()[0]==checksum
    report={'source_commit':commit,'zip':Path(path).name,'bytes':Path(path).stat().st_size,'sha256':checksum,
            'files':len(names),'runtime_baseline':baseline,'audit':'PASS'}
    print(json.dumps(report,indent=2))
    return report
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('zip'); a=ap.parse_args(); audit(a.zip)
