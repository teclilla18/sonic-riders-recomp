# Released state (2026-11-18)

**Installer maintenance released in existing downloads:** shared PC/original Switch/Android GUI and native
Switch 2 Alpha 2 are responsive, preserve the full banner, handle live DPI/resize, wrap logs/instructions,
show consistent completion and keep Open Output Folder visible after success. Source-only packages were
replaced in the existing releases; versions/tags/descriptions/other assets unchanged. Linux/iOS untouched.
Package source: stable `cf243a9f`, native `d680eb2b`. All 176 simulated DPI/state cases and real output-button
clicks pass; four independent cold Sandbox GUI/space-path installs pass (native CLI/user-data repeat too).
Full ZIP/source/privacy audits and anonymous public SHA-256 download readbacks pass. Physical multi-monitor
migration is untested; no additional hardware gameplay validation. Details: docs/notes/installer-layout.md.
Current-tree/package guards pass; pre-existing history-only CI rejects older banner blobs. The guard and
repository history were preserved.

**Released:** [v0.8.7 Stable](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7),
original runtime/tag source `381a3694e65ddcbaa0a42f7479c8d72ef324ba1c`, verified latest stable.
Windows, original Switch, Android and Linux source setup ZIPs passed source/data audits and full uploaded
SHA-256 readback. Cold Windows Sandbox PC and Android tests passed separately, including space paths.
No new iOS package. Existing iOS downloads remain available.

**Released separately:** [Switch 2 Alpha 2](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7-switch2-alpha.2),
original runtime/tag source `1ca54eb0897dc85687f5d7b0e3f93e4b7ba71a66`, marked prerelease, not latest stable.
ZIP SHA-256: `dde6e5125981ecee948af030b45076431289eaceadfdd049579a567c1f1aa6d7`.
Cold native GUI and command-line/space-path builds, user-data preservation sentinels and uploaded readback
audit passed. These tests establish setup/build behavior, not additional hardware gameplay acceptance.
Runtime/config match accepted source `80304ca` / build `0d995f3def88` exactly. Fresh installs default to
60 FPS; Unlimited rendering remains explicitly experimental. Updates preserve existing settings.

**Implemented, tested and released:** Super Sonic's 999-ring compatibility, native-level-preserving Lv. 4/5
progression, noncompounding mod-compatible balance, adaptive multiplayer HUD, multiplayer Lv. 4/5 digits
(#29), independent shared 2P timer, and finished-race repeated stat/effect fix. Super top/boost/drift targets
are 195/270/110 Base, 220/300/125 Lv. 4 and 240/330/140 Lv. 5 km/h. Vanilla and online determinism preserved.
Main also includes validated Android Vulkan/shutdown fixes (#32) and Linux installer fix (#30).
PC matrix/controls and both own-view 6,701-checksum routes passed. Broader hardware coverage remains limited.
**Accepted by the user on Switch 2:** universal icon, Super Sonic feel, high-ring finish and 2P Fit HUD.
The console still has accepted build `0d995f3def88`; this release session did not replace or relaunch it.

**Unresolved Alpha 2 limitation:** Race Select (Free Race / Time Attack / World Grand Prix) can develop
severe persistent audio distortion, with one reported freeze at onset. Disabling Unlimited and leaving
the menu did not recover that session. Similar audio/stall failures predate these fixes; no severe new
regression was established. Native output drops/errors were zero; underruns, PCM faults and resource leaks
remain unproven. No audio fix is claimed. Captured log/screenshots are preserved locally under ignored
`build/live-preview-20261118/`; use log-reader rather than rereading logs. Rare post-race loading hang #6
remains open. No speculative fix or larger Super Sonic overhaul was attempted.

Updated easy-read HTML guides are committed and linked through GitHub. Existing Claude Artifact links
remain with an older-version notice. The user explicitly deferred same-URL republication until the weekly
reset and prohibited further browser automation this session. This does not block the published releases.

Retention completed: five newest releases retain their assets; v0.8.2 is permanently protected.
By explicit user choice, v0.8.1 and v0.8.3 remain iOS-only archive pages with their exact original iOS assets
and links. Eight obsolete non-iOS assets were removed only after complete local backups and hash checks.
v0.8.2 and v0.8.4 assets/notes were unchanged. Native source stays separate from main; dirty sr-main,
private DX and concurrent worktrees were preserved. Exact evidence and local ownership: excluded AI_HANDOFF.md.
