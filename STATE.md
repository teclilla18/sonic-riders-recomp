# State (2026-11-18)

Read this first, then excluded canonical AI_HANDOFF.md for ownership, deployed artifacts and rollback commands.
Preserve dirty sr-main, private/native experiments and the completed separate Android Issue32 work.
Code map: docs/MAP.md; reference index: docs/REFERENCE.md.

## Current local fixes and deployment

- **Implemented:** 2P shared clock reserved before player grouping; shallow boost backing joins its ring panel
  across the actual small gap. Only halves get the extra tolerance. Shared map/timer remain independent;
  existing quarter layouts, dimensions, copies, perspective and original4:3/online own-view behavior retained.
- **Implemented:** one deterministic guest mode/phase gate for level application and frame retirement.
  Finished/outside-race hooks restore owned stats once and discard pending feedback before native consumption;
  no Lv5 reapplication/rearming cycle. Active-race progression, constructor/reset and stat mods are retained.
- **Preserved:** Riders Plus Super top/boost/drift targets 195/270/110 Base, 220/300/125 Lv4 at 200 rings,
  240/330/140 Lv5 at 300. Unscaled-baseline ratios/mod compatibility/native0, fuel conversion/costs/999 ceiling
  and feedback behavior during active racing remain. Vanilla/standard mechanics and Unlimited are preserved.
- **Permanent native icon:** setup/icon_256.jpg, the universal cedry2kio game icon matching setup/icon.png.
  Direct JPEG dependency/fingerprint, manifest provenance, baseline 256x256 and exact NRO ASET validation.
  Launcher artwork/conversion was superseded at the user's direction; no artwork or original Switch path edited.
- **Deployed:** native 0d995f3def88 from 80304ca,29,491,891-byte NRO. Includes both runtime fixes and correct icon.
  Installed via OunceMTP with fresh local/SD backups, staged + installed NRO/manifest SHA256 readbacks.
  Unlimited pc_fps=-1 stays enabled; settings/env/pipeline-cache/save bytes unchanged; unrelated entries preserved.
  Immutable deployed artifact and exact rollback are in canonical local handoff. No agent launch on console.
- Previous db4c80467511 had the same runtime fixes but launcher icon; its matching pair is retained for rollback.
  First corrected-icon upload lost USB during staging, before promotion. After user reopened OunceMTP, both
  installed hashes were unchanged and no orphan stage existed; a fresh verified transaction succeeded.
  FTP fallback was checked but timed out. No blind retry or partial installed replacement occurred.
- **Authorization:** standing development/test device deployment permission in local AGENTS.md/AI_HANDOFF.md.
  Back up, preserve, verify, retain rollback and notify each install. Keep this work local: no push, merge,
  publication, public release/asset replacement or external reply. Public releases/assets still need approval.

## Validation and limits

- New actual-source regressions failed before fixes and pass afterward: actual overlapping2P clock/panel,
  tall/compact groups across five aspects, whole-racer finished-phase isolation/pending feedback, both hook
  orders, 100 repeated updates for standard/Super, plus all earlier balance/mod/reset/eight-racer tests.
- PC 2P 16:9 at 6250 and 32:9 at 6500: all 13 ring/boost pieces move together; all eight clock draws stay centred.
  Real race/pause captures complete; shared maps/covers retain coverage. No new runtime errors/fatals found.
- Exact previous-balance 3P/4P controls with matched capture schedule: full RAM, active and pause PNGs identical.
- Both own-views match all 6,701 checksum steps, full RAM, active/pause pixels. Deterministic offline routes do
  not establish live-peer acceptance; no network protocol/timing changes were made.
- Original finished-race reproduction with actual guest effect-branch counter: three entries before phase A,
  zero after (previously 230); zero repeated Lv5 events after finishing. Temporary instrumentation restored
  out of tracked/generated sources; final PC artifact rebuilt. Visible/audio hardware acceptance remains open.
- Three universal-icon validation tests and 10 deployment transaction/rollback regressions pass. Native full
  build validates header/source/resource identity, sidecar and exact embedded setup/icon_256.jpg bytes.
- **Not accepted/released:** hardware play of latest fixes/icon still needs the user's check. Eden cannot run
  native Switch 2. No hardware-performance claim; mode/clocks required with all physical FPS numbers.

## Prior fixes and separate work

- Earlier local work: Super live first block/native0 and both fuel ceilings, per-field ownership/reset,
  resource-neutral Super feedback, separate active MP4/5 digit pointers (#29), local split HUD grouping.
  Details and historical evidence: docs/notes/riders-plus-local-hud.md. Icon pipeline: docs/notes/switch2-icon.md.
- Stable v0.8.6, iOS lastv0.8.4; separate native Switch2 alpha has known audio/stutter issues. Personal Unlimited
  stays isolated; no native/private integration into main. PC texture packs accepted; Switch loading freezes.
- Inbox unchanged this turn:32 closed by separate Android work,6 loading hang remains open. No replies here.
- Other queued work: texture-pack background loading, stick spin #23, iOS zip, custom music #17, camera/roster,
  WGP #18, updater #31, online lobby/rules. Larger Super overhaul remains deferred.

## Next user checks

- Exit OunceMTP and check Homebrew Menu uses the universal game icon; verify About source 0d995f3def88.
- Play 2P boosts/notifications: timer centred, both ring/boost panels coherent. Spot-check 3P/4P layouts.
- Finish a high-ring race: no repeated level-up effects; then restart/change gear and test Super 200/300/999,
  Plus on/off. Capture with in-game Save screenshot; bring logs via /hw-test and log-reader.
