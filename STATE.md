# Switch 2 Alpha 1 release candidate (2026-11-17)

Isolated branch codex/switch2-alpha1, based on accepted f05fd81.
Package: SonicRiders-Switch2-Setup-Alpha1.zip.
Proposed title: Sonic Riders Recomp — Switch 2 Alpha 1.
Proposed tag: v0.8.6-switch2-alpha.1 (separate pre-release, not latest).
Do not push/merge/tag remotely/publish/announce before candidate approval.

## Baseline and status

- **Accepted:** runtime7d1bc5fcd419/f05fd81, physical firmware23.0.1 with reported
  kaiHEN20261117.1/Seedsow native homebrew, handheld mode0, clocks unrecorded.
  Tried gameplay/graphics/input/music/sound/overlay/enhancements and clean exit
  work. One tested route/environment is not universal compatibility evidence.
- **Excluded:** higher-FPS branchb381e54/build67341d614683. User reports Player
  Entry freeze selecting Sonic/Chaos Emerald gear; HOME/volume still work.
  Preserved separately. No FPS investigation/fix is authorized in this task.
- **Restored:** exact accepted NRO FTP-verified after saving failed-build logs,
  binary/manifest/env/settings locally. Onlypc_fps reset0; frames0 retained.
- **Implemented:** dedicated setup target/GUI, isolated compiler/build/SD path,
  pinned SDK and compiler dependencies, SD-ready manual-transfer output,
  update preservation; existing ISO/RVZ/extraction/recompilation reused.
- **Locally tested:** GUI compilation; native installation/platform validation,
  update preservation and cold-workdir source-commit/SDK-lock regressions.
- **Release gate:** exact source ZIP must pass the whole-archive audit and a
  genuinely cold Windows Sandbox GUI+CLI/space install. Ask before launching.
  Candidate checksum/commit and exact results live in the candidate validation
  record and local AI handoff. Sandbox cannot validate physical gameplay.

## Known limits and next step

Startup pipeline-cache saving can stall for seconds; no optimization included.
Other firmware/loaders, docking/sleep/multiplayer, all stages/story, Super Sonic
and long-run stability remain community tests. No universal FPS guarantee.
Default60, no automatic exit (frames0), hold Minus then Quit Game to hbmenu.
Public stable v0.8.6 and automatic updater channels must remain unchanged;
native alpha GUI uses manual updates. Installer docs: docs/SWITCH2-ALPHA.md.
Easy-read local guides updated; republishing existing Claude artifact URLs
requires external access/user help and publication approval. No announcements.
Preserve accepted artifact/checkpoint and experimental feature branch.
Inbox unchanged at start. Use log-reader for later log analysis.


Switch 2 Alpha1: fresh default2x; live resolution changes disabled exclusively on Switch2. Existing saved choices retained. Close game, edit settings.ini resolution=0 (1x) or2 (2x), restart. Live2x->1x permanently froze overlay while gameplay continued; underlying cause unconfirmed. Super Sonic race entry succeeded on accepted7d1bc5fcd419. Cold GUI/CLI/space testing passed for prior candidate7517958; final restricted candidate requires a new cold validation.
