# Illustrated console booklets: sources and verification

Checked November 18, 2026. These are documentation changes, not a new game release.

## Which packages the steps describe

| Platform | Public release | Exact setup asset | SHA-256 |
| --- | --- | --- | --- |
| Nintendo Switch | [Stable v0.8.7](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7) | `SonicRiders-Switch-Setup-v0.8.7.zip` | `ad21ee145873bfec161d4ecf8600a21724cb9429158b3b08ff1bd00538fb4687` |
| Nintendo Switch 2 | [Experimental Alpha 2](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7-switch2-alpha.2) | `SonicRiders-Switch2-Setup-Alpha2.zip` | `dde6e5125981ecee948af030b45076431289eaceadfdd049579a567c1f1aa6d7` |

The current installers were maintenance replacements in these existing releases. Their installer source is
stable `cf243a9fec432d5ecd0ab5631a70b0a0ac5b99a6` and native
`d680eb2b78ec26a0ca7883b2ebf54471893135b2`; their immutable runtime release tags did not move.
The two full public ZIP downloads were checked against GitHub's SHA-256 digests and their ZIP CRCs.
No release package was rebuilt or changed for these guides.

## Primary source checks

- Claude's existing [easy-read install guide](install-guide.html) and [update guide](update-guide.html):
  primary visual reference. The new booklets retain their warm background, original color meanings,
  rounded panels, numbered steps, checkpoint language, readable Atkinson Hyperlegible body font and
  Lexend headings. The new pages add actual annotated installer pictures and labelled original drawings.
- Current published installer source: [stable launcher](https://github.com/teclilla18/sonic-riders-recomp/blob/cf243a9fec432d5ecd0ab5631a70b0a0ac5b99a6/launcher/launcher.cpp)
  and [native launcher](https://github.com/teclilla18/sonic-riders-recomp/blob/d680eb2b78ec26a0ca7883b2ebf54471893135b2/launcher/launcher.cpp).
  Checked button labels, selection/required-option behavior, SD detection, first-install disc-copy option,
  completion, output-folder locations, manual transfer, license and icon differences.
- Published `setup/setup.ps1`: supported ISO/GCM/RVZ/WIA, USA revision and full-image hash checks,
  download/build steps, PC space requirements, destination files and update-copy behavior.
- Native [SD installer](https://github.com/teclilla18/sonic-riders-recomp/blob/d680eb2b78ec26a0ca7883b2ebf54471893135b2/tools/switch2_install.py):
  matching NRO/manifest, fresh settings/environment, existing-data preservation.
- Both platform implementations: game-image lookup accepts ISO/GCM directly in the fixed game folder;
  save path is `saves/card_a.raw`, settings path is `settings.ini`, mods path is `mods/`.
  RVZ/WIA conversion is a PC installer step, not a console runtime feature.
- `runtime/src/menu.cpp`: About displays the build version; Minus settings and Quit Game are the player route.
- Existing [installation reference](INSTALL.md), [alpha reference](SWITCH2-ALPHA.md),
  [installer maintenance validation](notes/installer-layout.md), and [native test history](notes/switch2-handoff.md).

## Verified outside setup resources

The project installs Sonic Riders, not console homebrew. Users must finish their platform's prerequisite
setup first. These resources are linked from the appropriate booklet; their compatibility requirements
can change. Read their current instructions rather than assuming a firmware version works.

- Original Switch: [Nintendo Homebrew's setup guide](https://switch.hacks.guide/),
  [full-memory title override instructions](https://switch.hacks.guide/user_guide/all/launching_cfw),
  and [hekate's USB storage documentation](https://github.com/CTCaer/hekate).
- Native Switch 2: [kaiHEN's official startup instructions](https://github.com/ouncebrew/kaihen#starting-it)
  and the [Seedsow entry-route project's setup](https://github.com/Malev0/seedsow-fs-kaihen#setup-once).
  kaiHEN documents native-game R launching and the second-launch behavior. Recorded project testing
  used firmware 23.0.1 / kaiHEN 20261117.1 through Seedsow / OunceShell; other setups are not established
  by that test. No firmware update or exploit setup was performed for these booklets.
- Own-disc dumping: [Wii Hacks Guide's illustrated CleanRip walkthrough](https://wii.hacks.guide/dump-games.html)
  and [CleanRip's project](https://github.com/emukidid/cleanrip). The GameCube subsection and compatible
  Wii disc drive are relevant. The booklets do not link to disc downloads or recommend shrinking the dump.
- Windows: [Microsoft's ZIP extraction instructions](https://support.microsoft.com/en-us/windows/experience/storage-filemanagement/zip-and-unzip-files)
  and [safe-removal instructions](https://support.microsoft.com/en-us/windows/hardware/safely-remove-hardware-in-windows).

## Installation reports reviewed

- [Discussion #20, Easier Install?](https://github.com/teclilla18/sonic-riders-recomp/discussions/20):
  existing project response confirms the original Switch hekate USB-storage alternative to removing the card.
  The main booklet route uses a card reader; the alternative is identified separately.
- [Issue #27, RVZ support](https://github.com/teclilla18/sonic-riders-recomp/issues/27): closed after RVZ
  support was added. Current conversion/rejection instructions were checked against the actual installer.
- [Issue #31, Switch update still showing 0.1.0](https://github.com/teclilla18/sonic-riders-recomp/issues/31):
  open; the latest reporter comment still asks for help. The cause is unresolved. The booklet gives
  verifiable destination/version checks and a manual update path, without claiming to fix this report.
- [Issue #6, rare Now Loading hang](https://github.com/teclilla18/sonic-riders-recomp/issues/6): open.
  A correct install does not establish that every loading hang is solved.
- Issue #24 was a Linux static-library build problem, confirmed by that reporter and fixed there.
  Its Linux package command is deliberately not presented as a Windows/Switch solution.
- Native Alpha 2's severe persistent Race Select audio distortion, freeze at onset, music/announcer loss
  and stutters remain disclosed. Normal 60 FPS is a starting default, not a verified audio cure.

## Picture provenance and privacy

All new pictures are under [images/installation/](images/installation/).

- `switch-installer.png` and `switch2-installer.png`: newly captured from the current published EXEs'
  actual idle windows. The stable window was also inspected through Windows computer use.
- `*-select-disc.png`, `*-select-card.png`, `*-options.png`: close-up crops of those same real windows.
  The card's private drive label is covered by an explicit `[hidden for privacy]` mask.
- `*-completion.png`: the real completion state after running the published GUI installer into a
  disposable SD folder. The build log is covered to remove personal paths. The relative output path,
  success text, next steps and real Open Output Folder button remain visible.
- Pink outlines, circles, numbered markers and arrows are precise SVG annotations composited over
  these captures. Padding keeps markers visible on small crops. No game/console screenshot was generated,
  and no simulated installer completion was used as an illustration of a successful real install.
- All `*.svg` drawings are original simple instruction diagrams. Every drawing says **ILLUSTRATION ·
  NOT A SCREENSHOT**. Console launch/title/menu diagrams are not hardware captures.
- GitHub's release asset list could not be captured reliably because its lazy loader failed in the
  isolated browser. The download pages therefore use clearly labelled diagrams, exact verified filenames
  and clickable canonical release/download links. No release-page screenshot was fabricated.
- Installer banner/icon art is the same already approved project material. Existing credits, including
  cedry2kio's game icon and the project's banner credits, remain in the README. This work does not add
  extracted game artwork or game frames.
- Font subsets are embedded as standard CSS data URLs for offline use, with their complete SIL Open
  Font License notices in [FONT-LICENSES.txt](images/installation/FONT-LICENSES.txt). No external font,
  script, stylesheet, image host, ChatGPT login or Claude login is needed by the new HTML booklets.

## Walkthrough and page validation

- **Confirmed:** stable published GUI installation, separate full ISO transfer to a disposable SD folder,
  verified ISO SHA-1, repeated update and byte-identical save/mod/cache/settings/environment sentinels.
- **Confirmed:** native published GUI installation in a direct physical cache, full ISO transfer and SHA-1,
  repeated manual update, and unchanged save/mod/cache/settings/environment sentinels.
- **Confirmed:** real Open Output Folder footer mouse clicks and successful ShellExecute for both current
  published EXEs in an isolated completion UI fixture. These fixture captures are not public guide images.
- **Confirmed:** both 17-checkpoint HTML/Markdown booklets pass 225 local link/image checks; all 16
  external guide/download/support links return HTTP 200. Eight offline browser layouts (320/390/768/1280 px
  for both platforms) pass fonts/images, overflow, theme, checkbox, help and print-width checks.
  Console gameplay is outside these installation checks.
- The first local native fixture used a directory junction for an isolated cache. Its configure step
  reported a `relative_to` path mismatch. The test was corrected to a direct physical cache directory.
  The published package/source was not modified to make the test pass. A first stable fixture also needed
  its child process's normal Windows PowerShell module search path restored; no system settings changed.
- These are warm-cache Windows walkthroughs, not new cold-machine claims. The exact packages also have
  earlier independent cold Windows Sandbox GUI/space-path evidence in the installer maintenance note.
- The static checker verifies local links, anchors, image alternatives, SVG XML, size limits, checkpoints,
  privacy and separation of the two platforms. Browser checks cover phone/tablet/desktop widths,
  offline font/image loading, no page overflow, checkpoint controls, help expansion, dark mode and print.
- **Not tested here:** newly dumped physical discs, SD-reader hardware variations, homebrew setup from
  an unmodified console, actual SD ejection, physical console launch/gameplay, or child-user acceptance.
  Existing console reports are labelled separately. No connected console was overwritten or relaunched.

## Maintaining the booklets

Edit [the shared content source](../tools/console_guide_data.py), then run:

```text
python tools/make_console_guides.py
python tools/check_console_guides.py
```

HTML and Markdown are generated together to keep filenames, warnings and steps in sync. The generator
does not capture or invent screenshots. New installer versions need freshly checked source/downloads
and reviewed captures before changing the guide's version banner. Keep every exact screenshot hash
in the existing game-data guard's artwork allowlist; never loosen its rules or commit original logs.

## Reviewed screenshot fingerprints

These exact reviewed files are allowlisted; all other guard rules stay unchanged.

| File | Pixels | SHA-1 |
| --- | --- | --- |
| [switch-completion.png](images/installation/switch-completion.png) | 1314 × 809 | `117e009d71e381fddb4c4cb6332ece8c87c3f63d` |
| [switch-installer.png](images/installation/switch-installer.png) | 1314 × 809 | `c99dbc9b73cd2eff24ca90320837d46c7323a15d` |
| [switch-options.png](images/installation/switch-options.png) | 560 × 428 | `d3f833422dd4516892cf3c3d6f0081adcc328ad3` |
| [switch-select-card.png](images/installation/switch-select-card.png) | 565 × 233 | `11edb8b1b22b4918e675a8ab53135bda53652b70` |
| [switch-select-disc.png](images/installation/switch-select-disc.png) | 565 × 193 | `61022475a40e09d810c6109c18162d854e26b1c4` |
| [switch2-completion.png](images/installation/switch2-completion.png) | 1314 × 809 | `871abff16aa48ed974e3197b4ff43e4ed9fcfb43` |
| [switch2-installer.png](images/installation/switch2-installer.png) | 1314 × 809 | `2d0427e65a7c73bfdd9d84250ac27995cd60bf60` |
| [switch2-options.png](images/installation/switch2-options.png) | 560 × 428 | `38d20665c4661e4bf94671f9de4b58b0d6d2df55` |
| [switch2-select-card.png](images/installation/switch2-select-card.png) | 565 × 233 | `3415e8d035862c907f3cdeefe8bd3dbe7e84a600` |
| [switch2-select-disc.png](images/installation/switch2-select-disc.png) | 565 × 193 | `eff37dd176652aa70c7c8977d04d0e95f70abfcb` |
