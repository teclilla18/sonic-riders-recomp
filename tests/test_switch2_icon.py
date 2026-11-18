"""Icon conversion and real NRO asset verification; no disc or game data needed."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('icon', ROOT / 'tools/switch2_icon.py')
icon = importlib.util.module_from_spec(spec); spec.loader.exec_module(icon)


class IconTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.output = Path(cls.tmp.name) / 'icon.jpg'
        icon.build(ROOT / 'launcher/icon_256.png', cls.output)
        cls.jpeg = cls.output.read_bytes()

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def test_conversion_and_noop_rebuild(self):
        self.assertEqual(icon.jpeg_size(self.jpeg), (256, 256))
        stamp = self.output.stat().st_mtime_ns
        icon.build(ROOT / 'launcher/icon_256.png', self.output)
        self.assertEqual(self.output.stat().st_mtime_ns, stamp)

    def test_rejects_wrong_source_size_and_incomplete_jpeg(self):
        bad = Path(self.tmp.name) / 'wrong.png'
        data = bytearray((ROOT / 'launcher/icon_256.png').read_bytes())
        struct.pack_into('>II', data, 16, 32, 32); bad.write_bytes(data)
        with self.assertRaises(ValueError): icon.build(bad, Path(self.tmp.name) / 'bad.jpg')
        for value in (b'', b'\xff\xd8\xff\xff\xff\xd9', self.jpeg[:-2]):
            with self.assertRaises(ValueError): icon.jpeg_size(value)

    def test_embedded_asset_bounds_and_exact_bytes(self):
        data = bytearray(0x100 + 56 + len(self.jpeg))
        data[16:20] = b'NRO0'; struct.pack_into('<I', data, 0x18, 0x100)
        data[0x100:0x108] = b'ASET\0\0\0\0'
        struct.pack_into('<QQ', data, 0x108, 56, len(self.jpeg))
        data[0x138:] = self.jpeg
        icon.verify_nro_icon(data, self.jpeg)
        with self.assertRaises(ValueError): icon.verify_nro_icon(data[:-1], self.jpeg)
        data[-1] ^= 1
        with self.assertRaises(ValueError): icon.verify_nro_icon(data, self.jpeg)


if __name__ == '__main__': unittest.main()
