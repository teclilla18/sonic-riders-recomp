# Sonic Riders Recomp — Switch 2 Alpha 1

**Experimental native Switch 2 pre-release**, intended for community bug
reporting, compatibility, performance and stress testing. Stable v0.8.6 and
the original Switch/PC/Android/Linux/iOS installers remain separate.

Download **SonicRiders-Switch2-Setup-Alpha1.zip** and its SHA-256 file. This is
a Windows setup/source package, not a compiled game. It builds locally from
your own validated USA GXEE8P revision0 disc image (ISO/GCM or supported RVZ/WIA).
No game files, generated game code, compiled NRO, console keys or saves are
included. See [the alpha guide](../SWITCH2-ALPHA.md) for GUI/CLI installation,
manual transfer, update preservation, controls and bug reports.

The runtime uses accepted checkpointf05fd81/build7d1bc5fcd419. Reported physical
environment: firmware23.0.1, kaiHEN20261117.1 via Seedsow/native-game hold-R;
handheld, clocks unrecorded. Gameplay/input/graphics/music/sound/overlay and
tried enhancements, shutdown and relaunch were accepted on the tested route.
This is not a guarantee for all firmware, loaders, gears or modes.

Higher-FPS interpolation is **excluded**; a separate experimental build froze
at Player Entry with Sonic/Chaos Emerald gear. Alpha retains the accepted
renderer, defaults to60, and has no automatic exit cap. Hold Minus~0.5s,
choose Quit Game and confirm to return to hbmenu.

Known risks: startup/cache-save hitches, incomplete long-play/stage/story/
multiplayer/docking/sleep/gear coverage and limited hardware compatibility data.
Please report build ID, firmware/loader, mode/clocks, precise reproduction
steps and reviewed runtime/lifecycle logs. Never attach disc/game binaries,
extracted assets, saves or keys. Installation build checks in Windows Sandbox
do not prove Switch 2 hardware functionality.

Credits: ouncebrew/ounce-devkit, libounce, OunceVK, kaiHEN, Seedsow/OunceShell,
devkitPro, shaderc/NDK, decomp-toolkit and Dear ImGui developers. Dusklight and
Aurora informed platform patterns; their game code/dependencies are not linked.
LLM-assisted development with Claude Code/Codex and human hardware testing.
This is unofficial and unaffiliated with SEGA/Nintendo.

Candidate record supplies the exact source commit, dependency versions,
ZIP SHA-256 and cold GUI/CLI/space-validation results before publication.
Proposed tag `v0.8.6-switch2-alpha.1` must resolve to that exact packaged commit;
create only a pre-release, never replace stable assets or mark alpha latest.
Local easy-read guides are prepared; their existing public Claude artifact
URLs require external access/user assistance to republish after approval.

Draft only: no remote tag, upload, release or announcement is authorized yet.


Switch 2 Alpha1: fresh default2x; live resolution changes disabled exclusively on Switch2. Existing saved choices retained. Close game, edit settings.ini resolution=0 (1x) or2 (2x), restart. Live2x->1x permanently froze overlay while gameplay continued; underlying cause unconfirmed. Super Sonic race entry succeeded on accepted7d1bc5fcd419. Cold GUI/CLI/space testing passed for prior candidate7517958; final restricted candidate requires a new cold validation.
