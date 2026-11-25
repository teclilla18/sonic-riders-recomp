"""Assemble notices and corresponding source for the standalone beta payload."""

import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import tomllib
from zipfile import ZIP_DEFLATED, ZipFile

from package_poc import DEPS, ROOT, WORK, verify

TERMUX_SOURCES = {
    "libiconv": ("libiconv-1.19.1.tar.gz", "9a06844846d664d7122161700c10a64480d7c7bcead3091925a785b38c91edab",
                 "4597ac943f79d018079d286ae8c27d27bd1eb55b"),
    "zstd": ("zstd-1.6.0.tar.gz", "289bb207935e5b4f241239f9832e665fa6b9636c3f87f479e125371e13842b8e",
             "5b6ce0184f11681c5121ca822a6e68bcff2ac749"),
}
UPSTREAM_NOTICES = (
    "CPython-LICENSE.txt", "Chaquopy-LICENSE.txt", "OpenSSL-LICENSE.txt",
    "SQLite-copyright.html", "LLVM-LICENSE.txt", "MPL-2.0.txt",
)


def add_termux_sources(z, licenses):
    for name, (filename, digest, revision) in TERMUX_SOURCES.items():
        source = verify(licenses / filename, digest)
        recipe = licenses / f"termux-{name}"
        if (recipe / "TERMUX-REVISION.txt").read_text().strip() != revision:
            raise ValueError(f"Unrecognized Termux recipe for {name}")
        if digest not in (recipe / "build.sh").read_text():
            raise ValueError(f"Termux source hash does not match for {name}")
        z.write(source, f"sources/{name}/{filename}")
        for path in sorted(recipe.iterdir()):
            if path.is_file():
                z.write(path, f"sources/{name}/termux-recipe/{path.name}")
        with tarfile.open(source, "r:gz") as archive:
            for entry in archive:
                _, _, file = entry.name.partition("/")
                if entry.isfile() and "/" not in file and file.startswith(("LICENSE", "COPYING")):
                    z.writestr(f"notices/{name}/{file}", archive.extractfile(entry).read())


def is_notice(path):
    return path.is_file() and path.name.upper().startswith(
        ("LICENSE", "LICENCE", "COPYING", "COPYRIGHT", "NOTICE", "UNLICENSE", "AUTHORS"))


def add_rust_notices(z, metadata, upstream):
    graph = json.loads(metadata.read_text(encoding="utf-8-sig"))
    nodes = {node["id"] for node in graph["resolve"]["nodes"]}
    lock = DEPS / "dtk" / "Cargo.lock"
    locked = {(entry["name"], entry["version"]): entry
              for entry in tomllib.loads(lock.read_text(encoding="utf-8"))["package"]}
    index = WORK / "cargo" / "registry" / "cache"
    inventory = []
    for package in sorted(graph["packages"], key=lambda p: (p["name"], p["version"])):
        if package["id"] not in nodes:
            continue
        name = f'{package["name"]}-{package["version"]}'
        folder = Path(package["manifest_path"]).parent
        expression = package["license"] or package["license_file"]
        inventory.append(f"{name}\t{expression}\t{package['repository'] or ''}")
        paths = {path for path in folder.iterdir() if is_notice(path)}
        if package["license_file"]:
            paths.add(folder / package["license_file"])
        if not paths:
            paths.update(path for path in (upstream / name).glob("*") if path.is_file())
        if not any(is_notice(path) for path in paths):
            raise ValueError(f"No license or attribution file located for {name}")
        for path in sorted(paths):
            z.write(path, f"notices/dtk-crates/{name}/{path.name}")
        if "MPL" in expression:
            crate, = index.glob(f"*/{name}.crate")
            record = locked[(package["name"], package["version"])]
            z.write(verify(crate, record["checksum"]), f"sources/dtk-crates/{crate.name}")
    z.write(lock, "notices/dtk-crates/Cargo.lock")
    z.writestr("notices/dtk-crates/inventory.tsv", "Package\tLicense\tSource\n" + "\n".join(inventory) + "\n")
    return len(inventory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", required=True, type=Path)
    args = parser.parse_args()
    tools = args.tools
    licenses = DEPS / "licenses"
    destination = licenses / "standalone-third-party.zip"
    pending = destination.with_suffix(".pending")
    ndk = tools / "android-sdk/ndk/27.3.13750724"
    with ZipFile(pending, "w", ZIP_DEFLATED, strict_timestamps=False) as z:
        z.write(ROOT / "LICENSE", "notices/Riders-Plus-MPL-2.0.txt")
        z.write(Path(__file__).with_name("THIRD_PARTY.md"), "README.md")
        z.write(ndk / "NOTICE", "notices/Android-NDK-r27d-NOTICE")
        z.write(ndk / "NOTICE.toolchain", "notices/Android-NDK-r27d-NOTICE.toolchain")
        z.write(tools / "sdl2-src/LICENSE.txt", "notices/SDL2-2.32.10.txt")
        z.write(tools / "imgui/LICENSE.txt", "notices/Dear-ImGui-1.92.9b.txt")
        for name in UPSTREAM_NOTICES:
            z.write(licenses / name, f"notices/{name}")
        add_termux_sources(z, licenses)
        count = add_rust_notices(z, WORK / "dtk-license-metadata.json", licenses / "dtk-upstream")
    pending.replace(destination)
    with destination.open("rb") as stream:
        print(f"{count} dependency license records, SHA-256 {hashlib.file_digest(stream, 'sha256').hexdigest()}")


if __name__ == "__main__":
    main()
