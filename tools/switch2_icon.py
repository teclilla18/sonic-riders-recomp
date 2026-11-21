"""Validate the universal game JPEG and its exact native Homebrew Menu NRO embedding."""
import argparse
from pathlib import Path
import struct


def jpeg_size(data):
    if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
        raise ValueError('Incomplete JPEG icon')
    pos = 2
    while pos + 4 <= len(data):
        if data[pos] != 255: raise ValueError('Invalid JPEG marker')
        while pos < len(data) and data[pos] == 255: pos += 1
        if pos + 3 > len(data): raise ValueError('Truncated JPEG marker')
        marker = data[pos]; pos += 1
        if marker in (0xDA, 0xD9): break
        length = int.from_bytes(data[pos:pos+2], 'big')
        if length < 2 or pos + length > len(data): raise ValueError('Truncated JPEG segment')
        if marker == 0xC0:
            if length < 8: raise ValueError('Truncated JPEG frame')
            height, width = struct.unpack_from('>HH', data, pos + 3)
            if (width, height) != (256, 256): raise ValueError('NRO JPEG must be 256 x 256')
            return width, height
        pos += length
    raise ValueError('NRO icon must be a baseline JPEG')


def verify_nro_icon(binary, icon):
    """ASET offsets are relative to the asset header, after the NRO image."""
    jpeg_size(icon)
    if len(binary) < 0x80 or binary[16:20] != b'NRO0': raise ValueError('Invalid NRO image')
    base = struct.unpack_from('<I', binary, 0x18)[0]
    if base < 0x80 or base + 56 > len(binary) or binary[base:base+8] != b'ASET\0\0\0\0':
        raise ValueError('Missing NRO asset header')
    offset, size = struct.unpack_from('<QQ', binary, base + 8)
    if offset < 56 or base + offset + size > len(binary) or binary[base+offset:base+offset+size] != icon:
        raise ValueError('Embedded NRO icon differs from universal game JPEG')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--nro', type=Path)
    args = ap.parse_args()
    jpeg_size(args.source.read_bytes())
    if args.nro: verify_nro_icon(args.nro.read_bytes(), args.source.read_bytes())
