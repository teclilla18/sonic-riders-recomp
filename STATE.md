# Switch 2 Alpha 2 installer maintenance (2026-11-18)

Released v0.8.7-switch2-alpha.2 remains at immutable tag 1ca54eb0; public stable v0.8.7 stays latest.
Runtime/config and SDK pins remain identical to the accepted Alpha2 baseline80304ca/build0d995f3def88.
The user accepted the universal game icon, Super Sonic feel, high-ring finish and2P Fit HUD.
Fresh60FPS/2x defaults and existing settings/saves/caches/env preservation are unchanged.

Implemented: shared responsive Windows installer, uncropped aspect-correct artwork, wrapped logs,
live DPI handling, safe swap-chain resizing, clear completion and persistent Open Output Folder.
Native messages identify the correct Switch2 SD-ready directory and native-game hbmenu startup.
Tested:44 off-screen idle/building/done/failed cases across11 geometries and100–250% simulated DPI;
actual maximize/restore/DPI-message handlers and output-folder mouse click/ShellExecute pass.
Three native preservation/input tests and actual no-SD completion-message branch pass.
The exact rebuilt source ZIP still requires cold GUI/CLI/space-path validation and distribution audit.
User authorizes replacing only this existing ZIP/checksum after validation; no version/tag/description changes.

Known severe RaceSelect audio distortion remains unresolved; no runtime/audio work in this task.
Existing Claude Artifact republication remains deferred; preserve all pending canonical handoff entries.
See docs/notes/installer-layout.md and the excluded canonical AI_HANDOFF.md for current evidence/ownership.
