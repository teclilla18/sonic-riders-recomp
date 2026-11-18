"""Exercise transaction failures against a fake device; no game data or USB required."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('deploy', Path(__file__).parents[1] / 'tools/switch2_deploy.py')
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
FOLDER = d.DEFAULT_REMOTE


class Device:
    def __init__(self):
        self.files = {FOLDER + '/' + n: b'old-' + n.encode() for n in d.NAMES}
        self.files[FOLDER + '/saves/settings.ini'] = b'preserve'
        self.writes = []
        self.corrupt = False
        self.fail_promote = False

    def list(self, path):
        return [{'name': k[len(path) + 1:], 'folder': False, 'size': len(v)}
                for k, v in self.files.items() if k.startswith(path + '/') and '/' not in k[len(path) + 1:]]

    def get(self, remote, local):
        data = self.files[remote]
        if self.corrupt and '.codex-mtp-stage-' in remote: data += b'bad'
        with Path(local).open('xb') as f: f.write(data)

    def put(self, local, remote):
        if remote in self.files: raise ValueError('overwrite')
        self.files[remote] = Path(local).read_bytes(); self.writes.append(('put', remote))

    def rename(self, remote, name):
        target = str(Path(remote).as_posix().rsplit('/', 1)[0]) + '/' + name
        if self.fail_promote and '.codex-mtp-stage-' in remote: raise OSError('USB disconnected')
        if target in self.files: raise ValueError('overwrite')
        self.files[target] = self.files.pop(remote); self.writes.append(('rename', remote))

    def delete(self, remote): self.files.pop(remote)


class DeployTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.nro = self.root / d.NAMES[0]
        data = bytearray(256); data[16:20] = b'NRO0'; data[32:36] = b'OUN0'
        struct.pack_into('<II', data, 36, 2, 1); data[64:80] = b'src:012345abcdef'
        self.nro.write_bytes(data)
        Path(str(self.nro) + '.sha256').write_text(d.sha(self.nro) + '  ' + self.nro.name)
        self.manifest = self.root / 'build-info.json'
        d.save(self.manifest, {'format': 'sonicriders-switch2-phase2-v1', 'build_id': '012345abcdef'})
        self.device = Device()
        self.old = self.device.files.copy()
        self.expected = __import__('hashlib').sha256(self.old[FOLDER + '/' + d.NAMES[0]]).hexdigest()

    def run_deploy(self, **kw):
        return d.deploy(self.device, FOLDER, self.nro, self.manifest, self.root / 'backup', **kw)

    def test_plan_has_no_writes(self):
        self.assertFalse(self.run_deploy()['apply'])
        self.assertEqual(self.device.files, self.old)
        self.assertFalse((self.root / 'backup').exists())

    def test_apply_requires_hash(self):
        with self.assertRaises(ValueError): self.run_deploy(apply=True)
        self.assertEqual(self.device.writes, [])

    def test_wrong_current_build_stops_before_writes(self):
        with self.assertRaises(ValueError): self.run_deploy(apply=True, expected='0' * 64)
        self.assertEqual(self.device.files, self.old)
        self.assertTrue((self.root / 'backup/backup.json').exists())

    def test_roundtrip_preserves_backups_and_unrelated_files(self):
        state = self.run_deploy(apply=True, expected=self.expected)
        self.assertTrue(state['complete'])
        for name in d.NAMES:
            self.assertEqual(self.device.files[FOLDER + '/' + state['retained'][name]], self.old[FOLDER + '/' + name])
            self.assertEqual(self.device.files[FOLDER + '/' + name], (self.root / name).read_bytes())
        self.assertEqual(self.device.files[FOLDER + '/saves/settings.ini'], b'preserve')
        self.assertEqual(self.device.writes[-1][0], 'rename')

    def test_corruption_never_promotes(self):
        self.device.corrupt = True
        with self.assertRaises(ValueError): self.run_deploy(apply=True, expected=self.expected)
        for name in d.NAMES: self.assertEqual(self.device.files[FOLDER + '/' + name], self.old[FOLDER + '/' + name])
        self.assertFalse(any(op == 'rename' for op, _ in self.device.writes))

    def test_disconnect_retains_recovery_journal_and_old_bytes(self):
        self.device.fail_promote = True
        with self.assertRaises(OSError): self.run_deploy(apply=True, expected=self.expected)
        state = json.loads((self.root / 'backup/deployment.json').read_text())
        self.assertFalse(state['complete'])
        self.assertEqual(state['journal'][-1], ['retained', d.NAMES[0]])
        self.assertEqual(self.device.files[FOLDER + '/' + state['retained'][d.NAMES[0]]], self.old[FOLDER + '/' + d.NAMES[0]])

    def test_manifest_identity_sidecar_and_path_guards(self):
        self.manifest.write_text('{"build_id":"ffffffffffff"}')
        with self.assertRaises(ValueError): self.run_deploy(apply=True, expected=self.expected)
        for path in ('/switch/../config', '/config/game', '/switch//game', '/switch/game/'):
            with self.assertRaises(ValueError): d.remote_path(path)
        self.assertEqual(self.device.writes, [])

    def test_probe_cleans_own_files(self):
        result = d.probe(self.device, self.root / 'probe', size=1025)
        self.assertTrue(result['cleanup_verified'])
        self.assertEqual(self.device.files, self.old)

    def test_rollback_reuses_verified_backup_and_preserves_current_build(self):
        # Use a synthetic valid native pair as the installed old build.
        for name in d.NAMES: self.device.files[FOLDER + '/' + name] = (self.root / name).read_bytes()
        original = self.device.files.copy()
        previous_hash = d.sha(self.nro)
        self.run_deploy(apply=True, expected=previous_hash)
        result = d.rollback(self.device, FOLDER, self.root / 'backup', self.root / 'rollback', True, previous_hash)
        self.assertTrue(result['complete'])
        for name in d.NAMES: self.assertEqual(self.device.files[FOLDER + '/' + name], original[FOLDER + '/' + name])

    def test_rollback_rejects_corrupt_backup_without_writes(self):
        d.backup(self.device, FOLDER, self.root / 'backup')
        (self.root / 'backup' / d.NAMES[0]).write_bytes(b'corrupted')
        with self.assertRaises(ValueError):
            d.rollback(self.device, FOLDER, self.root / 'backup', self.root / 'rollback', True, self.expected)
        self.assertEqual(self.device.writes, [])


if __name__ == '__main__': unittest.main()
