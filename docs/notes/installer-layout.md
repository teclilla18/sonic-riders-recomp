# Windows installer layout (2026-11-18)

The shared Windows launcher serves PC, original Switch and Android setup; the isolated native Switch 2
branch carries the same UI changes. Linux uses its command-line setup; older Mac/iOS installers are separate.
Artwork, colors, branding and the Sonic Heroes promotion remain unchanged.

- Resize: unbind the Direct3D render target before resizing the swap chain; rebuild the target on success.
- DPI: process WM_DPICHANGED, apply its suggested rectangle, reset and scale style dimensions once, and
  use ImGui's scalable fonts. Minimum window tracking follows monitor DPI and available work area.
- Layout: cap the page at 1,400 logical pixels and reflow two columns to one below 900. The sidebar and
  page scroll independently; the action bar stays visible. Fit the complete banner with its original aspect
  ratio and full UV range. Long instructions, paths and build-log lines wrap within their available width.
- Completion: replace the active step with a completed message; label historical output BUILD LOG.
  Open Output Folder stays in the bottom bar after success. Local output folders are build/host-mingw,
  build/switch, build/android, and native build/switch2/sd/switch/sonicriders-switch2. It opens the local
  generated output even after a phone or SD card is disconnected; it does not create or alter user files.
- Native PowerShell messages distinguish original Switch from Switch 2, including no-SD/manual-copy and
  native-game hbmenu instructions. Runtime, SDK pins and installation preservation logic are unchanged.

## Verification

Build with `python tools/build_launcher.py`, then `python tools/test_installer_ui.py` (native branch:
`--modes switch2`). The disposable fixture tests idle/building/done/failed at eleven client sizes, including
900x620, 1080p, 1440p, 4K and 5120x1440, at 100/125/150/175/200/250% layout DPI. Each case maximizes,
restores and sends a DPI-change notification, checks client/backbuffer dimensions, and saves the app's own
restored and maximized PNGs under build/installer-ui. The real completion button receives mouse events
and opens each platform's temporary output folder through ShellExecute. User-file sentinels must match.
These are off-screen render and simulated monitor-DPI transitions, not physical multi-monitor acceptance.

`tools/sandbox_test.py --launcher --space` supports PC, Android and original Switch; the native branch also
supports Switch 2 and verifies SD/user-data preservation. Completion captures and output-folder paths are
returned with its results. Run cold tests on the exact rebuilt archives before replacing published assets.

Existing-release maintenance uses tools/make_release.py with the existing version and a new committed
installer source revision. Never move the release tag. Audit every ZIP entry against that source and the
normal game-data guard, verify the source-built installer, back up originals, retain filenames/descriptions,
regenerate only affected checksums, and verify full unauthenticated download readbacks after replacement.

Installer compilation maps source/library prefixes to relative names and omits the PE linker timestamp;
all three stable GUI copies must be byte-identical and contain no personal paths (ASCII or UTF-16).

## Existing release assets replaced and verified

The four Windows-hosted installers passed 176 render/state cases and real output-folder clicks, then
independent cold Windows Sandbox GUI installs and space-path repeats. The native repeat used its CLI
and verified that existing SD settings, saves, cache, environment and original Switch files were preserved.
The four ZIPs passed source/data/CRC/privacy audits and full anonymous public-download SHA-256 readback.
No new release/version/tag was created or moved; release descriptions/titles/flags and other assets remain.
Linux and older iOS downloads are unchanged. DPI transitions were simulated; physical multi-monitor
migration and hardware gameplay were not additional validation targets in this installer-only task.

| Existing asset | SHA-256 |
| --- | --- |
| SHA256SUMS-v0.8.7.txt | `d6f4a938d45d8d3ee9c923156be4f635dcfc0ac355822647ce34afb0f7f76054` |
| SonicRiders-Android-Setup-v0.8.7.zip | `12cdf75fe891258d99d6d4eb8ea2c1fa43556e7d9fa6e61834fee24bc7674720` |
| SonicRiders-PC-Setup-v0.8.7.zip | `17694bccea5f92c68d7e7b44ee4cf98dffb22aba16adcf5d3cbc618bd9ef1b9b` |
| SonicRiders-Switch-Setup-v0.8.7.zip | `ad21ee145873bfec161d4ecf8600a21724cb9429158b3b08ff1bd00538fb4687` |
| SonicRiders-Switch2-Setup-Alpha2.zip | `dde6e5125981ecee948af030b45076431289eaceadfdd049579a567c1f1aa6d7` |
| SonicRiders-Switch2-Setup-Alpha2.zip.sha256 | `3a5860db92004ea99aa282ed22e8eec3470072edc47a610f352b06e082a650a3` |

Stable installer package source: `cf243a9fec432d5ecd0ab5631a70b0a0ac5b99a6`. Native package source: `d680eb2b78ec26a0ca7883b2ebf54471893135b2`. Existing immutable release tags still identify their original runtime release sources.
