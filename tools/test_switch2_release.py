"""Targeted installer preservation and cold-build input regressions."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
import build_workdir
from switch2_install import install

ROOT=Path(__file__).resolve().parents[1]
class Switch2InstallerTests(unittest.TestCase):
    def test_native_install_and_update_preserve_user_data(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp); src=p/'source'; src.mkdir(); dest=p/'SD with spaces/switch/sonicriders-switch2'
            raw=bytearray(4096); raw[16:20]=b'NRO0'; raw[128:140]=b'OUN0'+struct.pack('<II',2,1); raw[256:260]=b'test'
            nro=src/'native.nro'; nro.write_bytes(raw)
            manifest=src/'info.json'; manifest.write_text(json.dumps({'format':'sonicriders-switch2-phase2-v1','build_id':'test'}))
            install(nro,manifest,dest)
            self.assertIn('SR_SWITCH2_FRAMES=0', (dest/'env.txt').read_text())
            files={'env.txt':b'SR_SWITCH2_FRAMES=0\r\n# customized\r\n','settings.ini':b'bloom=37\npc_fps=0\n',
                   'saves/test.txt':b'save sentinel','vk_pipeline_cache.bin':b'cache sentinel','disc.iso':b'not real game data'}
            for name,data in files.items():
                (dest/name).parent.mkdir(exist_ok=True,parents=True); (dest/name).write_bytes(data)
            original=dest.parent/'sonicriders/untouched.txt'; original.parent.mkdir(); original.write_text('original')
            install(nro,manifest,dest)
            for name,data in files.items(): self.assertEqual((dest/name).read_bytes(),data)
            self.assertEqual(original.read_text(),'original')
            raw[132:136]=struct.pack('<I',1); nro.write_bytes(raw)
            with self.assertRaises(ValueError): install(nro,manifest,dest)
            self.assertEqual((dest/'sonicriders-switch2.nro').read_bytes()[132:136],struct.pack('<I',2))

    def test_cold_workdir_has_commit_and_sdk_lock(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp); src=p/'release'; dst=p/'build with spaces'; src.mkdir()
            for n in ['configure.py','SOURCE_COMMIT','VERSION','diagnostics/switch2/dependencies.json']:
                f=src/n; f.parent.mkdir(exist_ok=True,parents=True); f.write_text('input '+n)
            build_workdir.sync(str(src),str(dst))
            for n in ['SOURCE_COMMIT','diagnostics/switch2/dependencies.json']:
                self.assertEqual((src/n).read_bytes(),(dst/n).read_bytes())

    def test_sdk_lock_matches_verified_baseline(self):
        tc=json.loads((ROOT/'setup/toolchain.json').read_text())
        lock=json.loads((ROOT/'diagnostics/switch2/dependencies.json').read_text())
        for key in ['url','sha256','version','commit']:
            self.assertEqual(tc['switch2']['sdk'][key],lock['sdk'][key])
        self.assertEqual(tc['android']['ndk']['sha256'],'82094f53e66a76b6a9ec4fc35a5076091a92de3b91d13c5d4a7cfdb226304c59')

if __name__=='__main__': unittest.main()
