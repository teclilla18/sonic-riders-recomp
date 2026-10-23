"""Extract the setup tool's downloads without depending on the system's tar/xz/zstd.

    python -I tools/setup_extract.py ARCHIVE DEST [--zstd-dir DIR]

Handles .zip, .tar.xz/.tar.gz/.tar.bz2 (standard library) and .tar.zst (the pinned 'zstandard'
wheel unpacked in DIR). Windows' own tar.exe lacks xz/zstd on many Windows versions and then
looks for xz.exe/zstd.exe on PATH, which most PCs do not have.
"""
import argparse
import os
import sys
import tarfile
import zipfile


def long_path(p):
    """absolute path with the \\\\?\\ prefix so deep toolchain paths work without LongPathsEnabled"""
    p = os.path.abspath(p)
    if os.name == "nt" and not p.startswith("\\\\?\\"):
        p = "\\\\?\\" + p
    return p


def extract_tar(path, dest):
    with tarfile.open(path, mode="r:*") as tf:  # detects gz/bz2/xz itself
        # 'data' refuses absolute paths and links leaving dest; symlinks that Windows refuses to
        # create are copied from their target by tarfile itself
        tf.extractall(dest, filter="data")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archive")
    ap.add_argument("dest")
    ap.add_argument("--zstd-dir", help="folder holding the unpacked 'zstandard' package (for .zst)")
    a = ap.parse_args()

    dest = long_path(a.dest)
    os.makedirs(dest, exist_ok=True)
    name = a.archive.lower()
    if name.endswith(".zip") or name.endswith(".whl"):
        with zipfile.ZipFile(a.archive) as zf:
            zf.extractall(dest)
    elif name.endswith(".zst"):
        if not a.zstd_dir:
            sys.exit("a .zst archive needs --zstd-dir")
        sys.path.insert(0, a.zstd_dir)
        import zstandard
        # to a plain .tar first: a seekable file lets tarfile copy link targets
        tmp = dest.rstrip("\\") + ".tar"
        try:
            with open(a.archive, "rb") as f, open(tmp, "wb") as out:
                zstandard.ZstdDecompressor().copy_stream(f, out)
            extract_tar(tmp, dest)
        finally:
            if os.path.exists(tmp):
                os.remove(tmp)
    else:
        extract_tar(a.archive, dest)


if __name__ == "__main__":
    main()
