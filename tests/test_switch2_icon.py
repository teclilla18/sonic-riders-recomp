"""Universal game icon and real NRO asset verification; no disc or game data needed."""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('icon', ROOT / 'tools/switch2_icon.py')
icon = importlib.util.module_from_spec(spec); spec.loader.exec_module(icon)


class IconTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.jpeg = (ROOT / 'setup/icon_256.jpg').read_bytes()

    def test_existing_universal_game_icon(self):
        self.assertEqual(icon.jpeg_size(self.jpeg), (256, 256))

    def test_rejects_wrong_source_size_and_incomplete_jpeg(self):
        data = bytearray(self.jpeg)
        struct.pack_into('>HH', data, data.index(b'\xff\xc0') + 5, 32, 32)
        with self.assertRaises(ValueError): icon.jpeg_size(data)
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
