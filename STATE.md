# State (2026-11-18)

Read this first, then excluded canonical AI_HANDOFF.md for ownership, exact local paths and deployments.
Durable references: docs/REFERENCE.md; code map: docs/MAP.md. Preserve dirty sr-main and private/native work.

## Current local review: Super Sonic Riders Plus and split-screen HUD

- **Implemented**, local codex/riders-plus-local-hud from public main bf9f723: Super uses his own first live stat
  block and native level 0. Gear ID is +0xBB (18), transformed character +0xBA (11); +0x1034 is an ability mask.
  Extend both fuel ceilings to 999 rings while preserving the conversion denominator and native costs.
- **Implemented:** existing Lv.4/5 multipliers/thresholds, field-wise stat ownership, reset before race/stat
  construction, restoration on decrease/disable/identity change, native feedback branch without air refill.
- **Implemented:** completed-frame renderer pass assigns shared ortho HUD draws to actual half/quadrant views,
  groups per owner, preserves dimensions, centred messages, timer/map and 3P silhouette. Original 4:3 and online
  own-view bypass it. No renderer guest-RAM writes, shader/backend/camera changes or changed EFB copies.
- **Separately implemented #29:** missing active MP digit pointers at 8182D528/8182D5F8. Old alternate HUD hooks
  never ran in tested local Free Race. Both Super and normal racers show 4/5 in mixed 3P PC captures.
- **Tested [confirmed]:** minimized PC deterministic 1P-4P x 4:3/16:9/16:10/21:9/32:9: 20 races, four captures each
  including pause. Actual-source synthetic tests cover thresholds, partial stat mods, disable, constructor/race
  reset, equal-valued new baselines, eight racers, clipping, shared pieces and original group dimensions.
- **Tested [confirmed]:** Super first block 182/100/250 -> 202.222/110/275 -> 217.389/125/300; decrease restores it.
  Near-999/over-cap fuel handled; native byte remains 0, refill 0. Plus off retains original stats. Final mixed 3P
  has exactly two Super feedback events and correct per-player digit hooks, with no reset/feedback spam.
- **Tested [confirmed]:** both offline own-view players match baseline on 6,701 whole-RAM checksum steps each,
  byte-identical 28 MiB snapshots and pixel-identical active/pause PNGs. Earlier local 3P RAM snapshot also equal.
- **Built:** full Windows and Switch targets; Android ARM64 affected-source compilation. Eden v0.1.1 renders 3P;
  all 48 Switch DKSH files scratch 0. No physical console/phone/iOS deployment or user acceptance.
- **Not released/accepted.** Keep source local: no push, merge, publication, release asset replacement or replies.
  User subsequently authorized deploying these fixes to Switch 2 over FTP. Prepare a separate native checkout
  from the currently installed personal baseline, back up NRO/manifest, preserve data/settings and verify readbacks.
  Details: docs/notes/riders-plus-local-hud.md; ignored evidence: build/.
- Before release: manual collection/boost/drift/damage/fall/capsules, consecutive races/gear changes, sound,
  results/story/other modes, real peers and physical hardware. Matrix captures do not establish full gameplay.

## Public releases and isolated work

- **Released:** stable v0.8.6 (2026-11-16), PC/Switch/Android/Linux setup zips; iOS last v0.8.4. PC one-click texture
  packs accepted; synchronous Switch pack loading still freezes. Android/iOS packs unplayed.
- **Released separately:** v0.8.6-switch2-alpha.1 experimental prerelease; scheduler/logging fixes, known audio and
  stutter issues. docs/notes/switch2-handoff.md is public continuity. Personal Unlimited remains separate and not
  hardware-accepted. This task changes neither native experiments nor existing deployed builds.
- External install/update Claude Artifacts still need republication/live verification; original URLs retained
  in public guides. No publication here. Keep v0.8.2 forever and five newest releases per standing rule.
- Issue #32 fallback remains on its isolated branch. New reporter comment 2026-11-18: a race completed successfully.
  No new independent device validation, merge, issue closure or reply here. #30 remains closed.

## Previously queued work (outside this task)

1. Thursday 2026-11-19 plan: Switch texture-pack background loading, off-by-default auto stick-spin (#23), iOS zip.
   Worker design/baseline and machine details remain in canonical handoff and older HISTORY entries.
2. Separate Android APK repository/disc-picker work needs its own authorized publication flow; no APK distribution
   from this repository and no private DX redistribution.
3. Hold-B boost, custom music (#17), right-stick camera, roster expansion, #18 WGP freeze and #31 updater follow-up.
4. Online lobby paused: room codes/list, punch/relay fallback and catch-up design agreed, not implemented. Next
   agreed work: room-wide 30 fps transition, host rules/stat mods, then lobby server. Protocol 4 PC-Switch and
   Switch-Switch 15,000-frame checks were previously confirmed; no live peer run in this task.

## Next review / hardware checks

- Review local fixes and separate MP digit correction before authorizing a push or merge.
- Play Super near 200/300/999 with Plus on/off: fuel costs, audible feedback, damage/fall/capsules, then change
  gear and restart. Capture through the port's own screenshot command.
- Play 2P/3P/4P wide and original 4:3: pause, finish/results, silhouette, centred messages, timer/map, gauge/digit
  spacing, speed blur and neighbouring views. Live online own-view still needs a manual check.
- Physical Switch numbers require docked/handheld and CPU/GPU/MEM clocks. PC/Eden cannot establish hardware speed.
  Switch 2 deployment is now authorized; playing/accepting the build remains the user's next step.
