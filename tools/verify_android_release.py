#!/usr/bin/env python3
"""Verify the published Android source/setup ZIP, or normalize a local rebuild's PE timestamp.

Only an explicitly named local output is written. No download, release, or game file is changed.
The normalized launcher and complete archive must match the recorded publication hashes.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify(path, manifest):
    data = path.read_bytes()
    if len(data) != manifest['archive_bytes'] or sha(data) != manifest['archive_sha256']:
        raise ValueError('archive size/SHA-256 differs from the published artifact')
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() or len(archive.infolist()) != manifest['archive_entries']:
            raise ValueError('archive integrity/entry count differs')
        version = archive.read(manifest['archive_prefix']+'VERSION').decode().strip()
        if version != manifest['embedded_version']:
            raise ValueError('embedded VERSION differs')
        if sha(archive.read(manifest['launcher_entry'])) != manifest['launcher_sha256']:
            raise ValueError('launcher SHA-256 differs')


def normalize_rebuild(source, output, manifest):
    if source.resolve() == output.resolve():
        raise ValueError('normalized output must differ from the input archive')
    with zipfile.ZipFile(source) as archive:
        if archive.testzip():
            raise ValueError('input archive failed integrity check')
        entries = [(copy.copy(info), archive.read(info)) for info in archive.infolist()]
    found = 0
    for i, (info, data) in enumerate(entries):
        if info.filename != manifest['launcher_entry']:
            continue
        found += 1
        image = bytearray(data)
        if image[:2] != b'MZ' or len(image) < 64:
            raise ValueError('launcher is not a PE executable')
        offset = struct.unpack_from('<I', image, 0x3c)[0]
        if image[offset:offset+4] != b'PE\0\0':
            raise ValueError('invalid launcher PE header')
        struct.pack_into('<I', image, offset+8, manifest['launcher_pe_timestamp'])
        if sha(image) != manifest['launcher_sha256']:
            raise ValueError('rebuilt launcher differs beyond the PE timestamp; check pinned tools/source')
        entries[i] = info, image
    if found != 1:
        raise ValueError('expected exactly one launcher entry')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name+'.part')
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for info, data in entries:
                # Match make_release.py's ZipInfo writes, including its compression settings.
                archive.writestr(info, data)
        verify(temporary, manifest)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[1]/
                        'docs/releases/v0.8.6-android-20261118.json')
    parser.add_argument('--normalized-out', type=Path, help='reproduce the recorded timestamp in a separate local ZIP')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    try:
        target = args.archive
        if args.normalized_out:
            normalize_rebuild(args.archive, args.normalized_out, manifest)
            target = args.normalized_out
        verify(target, manifest)
    except (ValueError, KeyError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: '+str(error)+'\n')
    print('PASS:', target.name, manifest['archive_sha256'], 'source', manifest['package_source_commit'])


if __name__ == '__main__':
    main()
