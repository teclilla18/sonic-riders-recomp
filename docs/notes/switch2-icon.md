# Native Switch 2 Homebrew Menu icon

The permanent game icon is `setup/icon_256.jpg`, the existing256x256 JPEG version of cedry2kio's
`setup/icon.png`. This is the universal game artwork used on the other platforms. `launcher/icon_256.png`
is launcher-specific artwork and must not be used for the native game NRO.

The original Switch implementation in `configure.py` passes this same JPEG to `elf2nro --icon` and lists
it as an NRO dependency. Native `tools/switch2_build.py` now follows that pattern directly. There is no
conversion, generated substitute, PowerShell image helper, or Pillow dependency.

## Permanent build and validation

- Reconfigure existing native graphs once with the usual `--target switch2` and pinned SDK/toolchain.
- Both version identity and NRO packaging depend on `setup/icon_256.jpg` in Ninja. The asset and validation
  helper are fingerprinted with the other build inputs. Asset changes trigger version/link/repack as needed.
- Configure validates the source JPEG is baseline256x256. Packaging passes its exact bytes through the
  existing native `elf2nro --icon` path and checks the NRO0/OUN0 header plus exact ASET icon bytes/bounds.
- Manifest icon provenance records `setup/icon_256.jpg`, its SHA-256 and256x256 dimensions. The build ID
  includes that asset hash, so using different artwork produces a different identity.
- `tools/switch2_icon.py setup/icon_256.jpg --nro build/switch2/sonicriders-switch2.nro` repeats validation.
  Missing, wrong-sized, truncated or differently embedded icons fail rather than falling back to the SDK icon.
- All NROs and extracted/generated verification artifacts remain ignored under build/. No artwork is modified.

`python tests/test_switch2_icon.py` uses the actual universal game JPEG and checks dimensions, rejection of
invalid/truncated data, exact embedded bytes and asset-section bounds. A full native build verifies the real
NRO. Transfer/structural checks do not by themselves confirm the Homebrew Menu display on hardware.

The [upstream elf2nro source](https://github.com/switchbrew/switch-tools/blob/master/src/elf2nro.c) documents
its icon argument and ASET section layout. Native platform/SDK/compiler settings remain unchanged.
