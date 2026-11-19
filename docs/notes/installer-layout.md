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
