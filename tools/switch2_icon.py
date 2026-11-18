"""Build the native hbmenu JPEG from our existing launcher PNG; verify NRO embedding."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def png_size(path):
    header = Path(path).read_bytes()[:24]
    if len(header) != 24 or header[:8] != b'\x89PNG\r\n\x1a\n' or header[12:16] != b'IHDR':
        raise ValueError('NRO icon source must be a PNG')
    size = struct.unpack('>II', header[16:24])
    if size != (256, 256): raise ValueError('NRO icon source must be 256 x 256')
    return size


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


def build(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    png_size(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix='.jpg', delete=False) as f:
        temp = Path(f.name)
    try:
        if os.name == 'nt':
            subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File',
                            str(Path(__file__).with_suffix('.ps1')), '-Source', str(source), '-Output', str(temp)],
                           check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        else:
            try:
                from PIL import Image
            except ImportError:
                raise SystemExit('Native NRO icon conversion requires Pillow on non-Windows hosts') from None
            with Image.open(source) as image:
                rgba = image.convert('RGBA')
                rgb = Image.new('RGB', rgba.size, (0, 0, 0)); rgb.paste(rgba, mask=rgba.getchannel('A'))
                rgb.save(temp, 'JPEG', quality=90, progressive=False)
        data = temp.read_bytes(); jpeg_size(data)
        if not output.exists() or output.read_bytes() != data: temp.replace(output)
    finally:
        temp.unlink(missing_ok=True)


def verify_nro_icon(binary, icon):
    """ASET offsets are relative to the asset header, after the NRO image."""
    jpeg_size(icon)
    if len(binary) < 0x80 or binary[16:20] != b'NRO0': raise ValueError('Invalid NRO image')
    base = struct.unpack_from('<I', binary, 0x18)[0]
    if base < 0x80 or base + 56 > len(binary) or binary[base:base+8] != b'ASET\0\0\0\0':
        raise ValueError('Missing NRO asset header')
    offset, size = struct.unpack_from('<QQ', binary, base + 8)
    if offset < 56 or base + offset + size > len(binary) or binary[base+offset:base+offset+size] != icon:
        raise ValueError('Embedded NRO icon differs from generated launcher JPEG')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path); ap.add_argument('output', type=Path)
    args = ap.parse_args()
    build(args.source, args.output)
