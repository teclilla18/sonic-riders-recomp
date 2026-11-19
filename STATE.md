# Switch 2 Alpha 2 installer maintenance (2026-11-18)

Released maintenance: existing Alpha 2 ZIP/checksum replaced after validation, package source `d680eb2b`.
Original release tag remains `1ca54eb0`; no new release/version/tag move. Stable v0.8.7 stays latest.
Runtime/config/SDK pins remain at accepted baseline `80304ca` / build `0d995f3def88`. No console deployment.

Implemented, tested and released: responsive shared installer layout, full aspect-correct artwork,
wrapped logs, live DPI handling, safe swap-chain resize, accurate native completion/no-SD paths and a
persistent Open Output Folder button. All 44 native UI cases and 176 combined cases pass at 100–250%
simulated DPI across 11 geometries. Actual button mouse clicks/ShellExecute and user-file sentinels pass.
Physical monitor migration was not manually tested. Cold GUI plus CLI/space-path native builds, source/SD
identity and preservation sentinels pass; source/data/privacy audit and anonymous SHA-256 readback verified.
Current-tree/package guards pass; pre-existing history-only CI still rejects older banner blobs.
The guard and repository history were preserved.

Fresh 60 FPS / 2x defaults and existing settings/saves/caches/environment preservation are unchanged.
User accepted the universal icon, Super Sonic feel, high-ring finish and 2P Fit HUD before this task.
Known severe Race Select audio distortion remains unresolved; no game rendering/audio/gameplay work here.
Linux/older iOS assets and all release descriptions/titles/flags were preserved. Original distributions
and metadata are retained locally for rollback; hashes are in docs/notes/installer-layout.md.
External Claude Artifact republication remains deferred; preserve canonical handoff pending entries.
