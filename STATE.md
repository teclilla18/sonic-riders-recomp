# State (2026-11-18)

Read this first, then excluded canonical AI_HANDOFF.md for ownership, exact local paths and deployments.
Preserve dirty sr-main, private/native experiments and the separate Issue32 agent's pending entries.
Code map: docs/MAP.md. Durable references: docs/REFERENCE.md.

## Current local review: Super balance and USB deployment tooling

- **Implemented:** Riders Plus-only Super Sonic active top/boost/drift targets, km/h:
  Base195/270/110; Lv4 at200 rings220/300/125; Lv5 at300 rings240/330/140.
  Scale captured unmodified fields by ratios to vanilla182/250/100. Applied ownership includes Base;
  repeated frames never compound. Partial stat mods and restoration/reset remain supported.
- Native level byte, ring/fuel conversion, costs, 999 ceiling and feedback are preserved. No acceleration,
  animation, board, HUD or other mechanic changes in this follow-up. Larger overhaul deferred.
- **Tested [confirmed]:** actual-source regressions cover thresholds/decreases,100 updates/tier, partial mods,
  Base disable/reset, standard racers and eight-racer ownership. Whole-racer comparison permits only the three
  speed fields to change. Unchanged actual-source full-frame HUD regression matrix also passes.
- **Tested [confirmed]:** minimized PC Super routes, Plus on/off, six captures each. Active block matches all
  requested targets; off remains182/250/100. Native0 throughout, exactly two on feedback events with refill0,
  zero off events, no repeated-message spam. Processes deliberately stopped after final captures.
- **Built:** Windows test executable and native Switch2 `7a05aa6f5f02` from local checkpoint4459752. Native
  header/embedded identity/manifest source hashes/sidecar validate. New balance not hardware-tested or accepted.
- **Implemented/tested:** Windows OunceMTP WPD utility with read-only listing/backups, temporary probes,
  explicit apply/current-hash gates, stage/readback/retain/promote/readback of NRO and manifest only.
  Shell65,537-byte and WPD33,554,449-byte SHA-256 roundtrips pass; wrapper1,048,593-byte probe passes;
  all temporary remote files removed. Rename/delete/overwrite refusal pass. Ten fake-transport tests cover
  corruption, disconnect, backup preservation, rollback and apply gates. Real installed promotion verified.
- Installed NRO/manifest each downloaded twice and match known hashes. Screenshots/log retrieved read-only.
  FTP fallback remains explicit; adapter implemented, not exercised this session. No automatic failover.
- **Authorization:** standing device development/test deployment permission now recorded in local AGENTS.md
  and AI_HANDOFF.md. Backup, preserve data/settings, verify, keep rollback and notify each installation.
  **Deployed:** balance `7a05aa6f5f02` via OunceMTP, staged/installed NRO+manifest hashes match. Previous
  `da8da376c5f4` pair retained locally/on SD; verified rollback plan available. Unlimited stays enabled;
  settings/env/pipeline-cache bytes unchanged, saves/unrelated entries preserved. User notified after install.
  No push, merge, publish, public release/asset change or external reply for this task.
  Exact utility commands, backups, build paths and recovery instructions are in local AI_HANDOFF.md and
  docs/notes/switch2-mtp.md. All captures/logs/binaries/RAM remain ignored under build/.

## Earlier local fixes and returned hardware results

- **Implemented:** Super own first live block/native0, both999-ring fuel ceilings, field-wise ownership/reset,
  resource-neutral native feedback; separate active MP digit pointers (#29); completed-frame local HUD groups.
  Details: docs/notes/riders-plus-local-hud.md. Public review branch codex/riders-plus-local-hud.
- **Previously tested:** PC20layout/aspect routes, mixed3P and both own-view players'6,701 full-RAM steps,
  identical snapshots/pixels. Windows/classicSwitch builds, Eden3P render, Android affected-source compile.
  Those checks predate this balance; current focused runtime tests and two PC routes are recorded above.
- **Hardware tested/user report:** installed prior native build has a 2P GUI problem; 3P/4P mostly fine.
  Returned2P screenshot confirms issue;4P settings overlay obscures centre. No explicit3P log evidence.
  Do not mark the complete HUD accepted/fixed. HUD code intentionally unchanged in the balance task.
- Returned old-build log has repeated Lv5 bookkeeping at548/568 rings, no proof of repeated visual effects;
  normal shutdown completes. Mode/clocks incomplete: no qualified hardware-performance claim.
- **Investigation confirmed:** actual PC2P trace and renderer probes expose shared timer/player0 grouping
  overlap; compact HUD can move inward. Finished-phase race reset/reapply causes redundant stat writes and
  repeated feedback rearming. Direct PC guest-branch probe counts230 effect-path entries during phaseA.
  Visible/sound/refill-store repetition not directly verified. Neither issue is fixed in installed7a05.
- Still need manual collection/costs/drift/damage/fall/capsules, audible feedback, consecutive races/gears,
  results/story/other stages and real peers. Hardware balance acceptance remains pending.

## Public releases and separate work

- Stable v0.8.6; iOS lastv0.8.4. PC one-click texture packs accepted; Switch synchronous loading still freezes.
  Separate v0.8.6-switch2-alpha.1 experimental prerelease has known audio/stutter issues. Personal Unlimited
  remains isolated. No public/native/private source integration in this task.
- Issue32 gained a Splash Canyon/Amy report and Tails/Green Cave post-race loading hang. Separate agent owns
  Android shutdown/release/main-integration work; consult canonical handoff, do not overwrite or duplicate it.
  Final inbox: that agent closed32 and added the loading report to6, which stays open. No replies from this task.
- External install/update Claude Artifacts still need republication and live verification.
- Other queued work: Switch texture-pack background loading, off-by-default stick spin (#23), iOS zip,
  hold-B boost, custom music (#17), right-stick camera, roster/WGP (#18), updater (#31), online lobby/30fps rules.

## Next concrete steps

- Review the2P HUD capture in ignored build/mtp/hardware-return before a separate authorized HUD fix.
- Investigation confirms2P timer/HUD group overlap and finished-phase reset/reapply; details/evidence in
  docs/notes/riders-plus-local-hud.md. Installed balance build does not include a fix for either issue.
- After authorized deployment: test Super Base/200/300/999 with Plus on/off, drift/boost and gear/race reset.
  Capture with the in-game screenshot option. Report mode/clocks for all hardware performance numbers.
