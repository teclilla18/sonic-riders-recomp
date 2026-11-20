"""Local USB MTP deployment with SHA-256 readback; FTP is an explicit fallback.

Default deploy is a read-only plan. Only --apply plus the expected installed NRO
hash permits replacing the NRO and build-info.json. No other installed names are
written. Exit the game before starting OunceMTP. See docs/notes/switch2-mtp.md.
"""
import argparse
import ftplib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('sonicriders-switch2.nro', 'build-info.json')
DEFAULT_REMOTE = '/switch/sonicriders-switch2'


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def save(path, data):
    Path(path).write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def remote_path(value):
    if (not value.startswith('/switch/') or '\\' in value or ':' in value
            or any(p in ('', '.', '..') for p in value[1:].split('/'))):
        raise ValueError('Expected a normalized path beneath /switch/')
    return value


class Mtp:
    def __init__(self, helper, device=None):
        self.base = [str(helper)] + (['--device', device] if device else [])

    def call(self, command, *args, write=False):
        cmd = self.base + (['--allow-installed'] if write else [])
        result = subprocess.run(cmd + [command, *map(str, args)], check=True,
                                capture_output=True, text=True, encoding='utf-8')
        return json.loads(result.stdout)

    def list(self, path): return self.call('list', path)
    def get(self, remote, local): self.call('get', remote, local)
    def put(self, local, remote): self.call('put-new', local, remote, write=True)
    def rename(self, remote, name): self.call('rename', remote, name, write=True)
    def delete(self, remote): self.call('delete', remote, write=True)


class Ftp:
    """No implicit protocol switching after a failed/partial transaction."""
    def __init__(self, host, port):
        self.ftp = ftplib.FTP()
        self.ftp.connect(host, port, timeout=60)
        self.ftp.login()
        self.ftp.voidcmd('TYPE I')

    def list(self, path):
        return [{'name': name, 'folder': facts.get('type') == 'dir',
                 'size': int(facts.get('size', 0))}
                for name, facts in self.ftp.mlsd(path) if facts.get('type') not in ('cdir', 'pdir')]

    def get(self, remote, local):
        with Path(local).open('xb') as f:
            self.ftp.retrbinary('RETR ' + remote, f.write)

    def absent(self, remote):
        p = PurePosixPath(remote)
        if any(x['name'].casefold() == p.name.casefold() for x in self.list(str(p.parent))):
            raise ValueError('Refusing to overwrite ' + remote)

    def put(self, local, remote):
        self.absent(remote)
        with Path(local).open('rb') as f:
            self.ftp.storbinary('STOR ' + remote, f)

    def rename(self, remote, name):
        target = str(PurePosixPath(remote).with_name(name))
        self.absent(target)
        self.ftp.rename(remote, target)

    def delete(self, remote): self.ftp.delete(remote)


def unique_files(transport, folder, names):
    entries = transport.list(folder)
    result = {}
    for name in names:
        found = [x for x in entries if x['name'].casefold() == name.casefold()]
        if len(found) != 1 or found[0]['folder'] or found[0]['name'] != name:
            raise ValueError('Missing, ambiguous or unexpected filename: ' + name)
        result[name] = found[0]
    return result


def readback(transport, remote, local, expected):
    transport.get(remote, local)
    actual = sha(local)
    if actual != expected:
        raise ValueError('SHA-256 readback mismatch: ' + remote)
    return actual


def backup(transport, folder, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    entries = unique_files(transport, folder, NAMES)
    hashes = {}
    for name in NAMES:
        target = output / name
        transport.get(folder + '/' + name, target)
        if target.stat().st_size != entries[name]['size']:
            raise ValueError('Backup size mismatch: ' + name)
        hashes[name] = sha(target)
        readback(transport, folder + '/' + name, output / ('verified-' + name), hashes[name])
    save(output / 'backup.json', {'remote': folder, 'sha256': hashes})
    (output / (NAMES[0] + '.sha256')).write_text(hashes[NAMES[0]] + '  ' + NAMES[0] + '\n')
    return hashes


def validate_build(nro, manifest):
    data = Path(nro).read_bytes()
    info = json.loads(Path(manifest).read_text(encoding='utf-8'))
    build_id = info.get('build_id', '')
    at = data[:4096].find(b'OUN0')
    if (data[16:20] != b'NRO0' or at < 0 or at + 12 > len(data)
            or struct.unpack_from('<II', data, at + 4) != (2, 1)
            or info.get('format') != 'sonicriders-switch2-phase2-v1'
            or not re.fullmatch('[0-9a-f]{12}', build_id)
            or ('src:' + build_id).encode() not in data):
        raise ValueError('Native Switch 2 header or embedded manifest identity mismatch')
    sidecar = Path(str(nro) + '.sha256')
    fields = sidecar.read_text().split() if sidecar.is_file() else []
    if not fields or fields[0].lower() != sha(nro):
        raise ValueError('Missing or mismatched NRO .sha256 build sidecar')
    return build_id


def deploy(transport, folder, nro, manifest, output, apply=False, expected=None):
    folder = remote_path(folder)
    build_id = validate_build(nro, manifest)
    unique_files(transport, folder, NAMES)
    sources = dict(zip(NAMES, map(Path, (nro, manifest))))
    hashes = {name: sha(p) for name, p in sources.items()}
    plan = {'build_id': build_id, 'remote': folder, 'sha256': hashes, 'apply': apply}
    if not apply:
        return plan
    if not expected or not re.fullmatch('[0-9a-fA-F]{64}', expected):
        raise ValueError('--apply requires --expected-installed-sha256')
    output = Path(output)
    old = backup(transport, folder, output)
    if old[NAMES[0]] != expected.lower():
        raise ValueError('Installed NRO changed; backup preserved, no remote writes made')
    token = uuid.uuid4().hex
    staged = {n: '.codex-mtp-stage-' + token + '-' + n for n in NAMES}
    retained = {n: n + '.before-mtp-' + token for n in NAMES}
    state = dict(plan, before_sha256=old, staged=staged, retained=retained,
                 journal=[], complete=False)
    journal = output / 'deployment.json'
    save(journal, state)
    def record(action, name):
        state['journal'].append([action, name]); save(journal, state)
    # Stage and verify BOTH files before the first installed filename changes.
    # Retain old SD copies and a local backup. A disconnect leaves this journal
    # for explicit recovery; never guess which version to delete on reconnect.
    for name in NAMES:
        transport.put(sources[name], folder + '/' + staged[name])
        record('staged', name)
        readback(transport, folder + '/' + staged[name], output / ('stage-' + name), hashes[name])
        record('stage-verified', name)
    # Recheck installed bytes immediately before promotion (also detects another writer).
    for name in NAMES:
        readback(transport, folder + '/' + name, output / ('before-promote-' + name), old[name])
    for name in NAMES:  # manifest last
        transport.rename(folder + '/' + name, retained[name]); record('retained', name)
        transport.rename(folder + '/' + staged[name], name); record('promoted', name)
        readback(transport, folder + '/' + name, output / ('installed-' + name), hashes[name])
        record('installed-verified', name)
    state['complete'] = True
    save(journal, state)
    return state


def rollback(transport, folder, source, output, apply=False, expected=None):
    source = Path(source)
    saved = json.loads((source / 'backup.json').read_text(encoding='utf-8'))
    if saved['remote'] != folder or any(sha(source / name) != saved['sha256'][name] for name in NAMES):
        raise ValueError('Rollback backup path or hashes do not match')
    return deploy(transport, folder, source / NAMES[0], source / NAMES[1], output, apply, expected)


def probe(transport, output, size=1024 * 1024 + 17):
    output = Path(output); output.mkdir(parents=True, exist_ok=False)
    name = '.codex-mtp-probe-' + uuid.uuid4().hex + '.bin'
    local = output / name; local.write_bytes(os.urandom(size))
    remote = '/switch/' + name
    # Clean up only this invocation's new random file, never wildcard matches.
    transport.put(local, remote)
    try:
        readback(transport, remote, output / 'readback.bin', sha(local))
        transport.rename(remote, name + '.renamed')
        readback(transport, remote + '.renamed', output / 'renamed-readback.bin', sha(local))
    finally:
        # A rename may have reached the device before its response was lost.
        for entry in transport.list('/switch'):
            if entry['name'] in (name, name + '.renamed') and not entry['folder']:
                transport.delete('/switch/' + entry['name'])
    if any(x['name'] in (name, name + '.renamed') for x in transport.list('/switch')):
        raise ValueError('Temporary probe cleanup not confirmed')
    result = {'bytes': size, 'sha256': sha(local), 'readback_verified': True,
              'rename_verified': True, 'cleanup_verified': True, 'installed_files_written': False}
    save(output / 'probe.json', result)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--transport', choices=('mtp', 'ftp'), default='mtp')
    ap.add_argument('--helper', type=Path, default=ROOT / 'build/mtp/switch2_wpd.exe')
    ap.add_argument('--device', help='Exact WPD id, only needed with multiple devices')
    ap.add_argument('--host', help='Explicit FTP fallback address; anonymous login')
    ap.add_argument('--port', type=int, default=1337)
    sub = ap.add_subparsers(dest='command', required=True)
    sub.add_parser('devices')
    ls = sub.add_parser('list'); ls.add_argument('path', nargs='?', default=DEFAULT_REMOTE)
    get = sub.add_parser('get'); get.add_argument('remote'); get.add_argument('local', type=Path)
    pr = sub.add_parser('probe'); pr.add_argument('--output', required=True, type=Path)
    for command in ('backup', 'deploy', 'rollback'):
        p = sub.add_parser(command)
        p.add_argument('--remote', default=DEFAULT_REMOTE)
        p.add_argument('--output', required=True, type=Path)
        if command != 'backup':
            if command == 'deploy':
                p.add_argument('--nro', required=True, type=Path)
                p.add_argument('--manifest', required=True, type=Path)
            else:
                p.add_argument('--backup', required=True, type=Path)
            p.add_argument('--apply', action='store_true')
            p.add_argument('--expected-installed-sha256')
    args = ap.parse_args()
    if args.transport == 'ftp' and not args.host: ap.error('FTP requires --host')
    t = Ftp(args.host, args.port) if args.transport == 'ftp' else Mtp(args.helper, args.device)
    if args.command == 'devices':
        if args.transport != 'mtp': ap.error('devices is MTP-only')
        result = t.call('devices')
    elif args.command == 'list': result = t.list(args.path)
    elif args.command == 'get':
        t.get(args.remote, args.local); result = {'sha256': sha(args.local)}
    elif args.command == 'probe': result = probe(t, args.output)
    elif args.command == 'backup': result = backup(t, remote_path(args.remote), args.output)
    elif args.command == 'rollback':
        result = rollback(t, remote_path(args.remote), args.backup, args.output,
                          args.apply, args.expected_installed_sha256)
    else:
        result = deploy(t, args.remote, args.nro, args.manifest, args.output,
                        args.apply, args.expected_installed_sha256)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, ftplib.Error) as exc:
        message = getattr(exc, 'stderr', None) or str(exc)
        raise SystemExit(message.strip()) from None
