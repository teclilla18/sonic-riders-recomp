#!/usr/bin/env python3
"""Install only native Switch 2 build products; retain existing user data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

def install(nro, manifest, destination):
    nro, manifest, destination = map(Path, (nro, manifest, destination))
    data = nro.read_bytes()
    record = data[:4096].find(b'OUN0')
    if data[16:20] != b'NRO0' or record < 0 or struct.unpack_from('<II', data, record+4) != (2, 1):
        raise ValueError('Not a native Switch 2 NRO (NRO0/OUN0 platform2/version1 required)')
    info = json.loads(manifest.read_text())
    if info.get('format') != 'sonicriders-switch2-phase2-v1' or info['build_id'].encode() not in data:
        raise ValueError('Native NRO and manifest do not match')
    destination.mkdir(parents=True, exist_ok=True)
    for source, name in [(nro, 'sonicriders-switch2.nro'), (manifest, 'build-info.json')]:
        target = destination/name
        if source.resolve() != target.resolve():
            temporary = destination/(name+'.installing')
            shutil.copyfile(source, temporary)
            if hashlib.sha256(temporary.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
                raise OSError('Installation copy checksum mismatch: '+name)
            temporary.replace(target)
    env = destination/'env.txt'
    if not env.exists():
        env.write_text('# Native Switch 2 Alpha: manual exit via Minus > Quit Game\nSR_SWITCH2_FRAMES=0\n', encoding='utf-8')
    settings = destination/'settings.ini'
    if not settings.exists():
        settings.write_text('screen=3\nresolution=2\npc_scale=2\nframerate=0\npc_fps=0\n', encoding='utf-8')
    print('Native Switch 2 SD-ready layout verified; existing env/settings/saves/caches retained.')

if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['nro','manifest','destination']: ap.add_argument('--'+name, required=True)
    a=ap.parse_args(); install(a.nro,a.manifest,a.destination)
