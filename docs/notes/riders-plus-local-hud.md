# Riders Crosswake Super Sonic and local HUD (2026-11-18)

Current follow-up implements the two defects investigated below:2P clock reservation/panel grouping and
finished-phase retirement before application/feedback. See the final section for tests and deployment.
Historical "not fixed" statements describe the prior balance-only build7a05aa6f5f02.

Implemented on the local review branch `codex/riders-plus-local-hud`, from public main `bf9f723`.
Not released or user-accepted. Keep source local: no merge, push, release changes or issue replies. The user
subsequently authorized a Switch 2 FTP deployment for a personal playtest; use an isolated native checkout,
back up the installed NRO/manifest, preserve data/settings and verify uploaded readbacks.

That personal native follow-up is now deployed from an isolated checkout: full pinned-SDK build and native
scheduler/Unlimited regressions pass; staged/installed NRO/manifest readbacks match, settings/env unchanged,
previous pair backed up locally/on SD. This is deployment verification; physical play/acceptance is pending.

Latest user steering: that installed build has now been played. The user reports a 2P GUI issue, with 3P/4P
mostly fine. The returned 2P in-game screenshot confirms the issue; the 4P screenshot is partly obscured by
the settings overlay. Keep HUD code unchanged in this balance task. No further installed replacement is
authorized at that checkpoint. The user subsequently granted standing development/test deployment permission
and authorized installing the balance build, then investigating2P placement and repeatedLv5 bookkeeping.
OunceMTP backups, apply/readback and rollback are documented in [switch2-mtp.md](switch2-mtp.md).

## Super Sonic: confirmed causes and implementation

- The old extended-level check required native level 2 and wrote block `+0x94C`. Super Sonic remains at native
  level 0 and uses block `+0x8DC`. Identify him with gear `+0xBB == 18` and transformed character `+0xBA == 11`;
  original character `+0xBF` stays 0. `+0x1034` is an ability mask (5 on both Blue Star and Super), not a gear ID.
- His ring word is a mirror of `floor(fuel * 100 / active max_air)`. The two native fuel clamps at `81815FEC`
  and `818160C8` otherwise limit it to 100. Extend only their ceiling to `max_air * ring_cap / 100`, preserving
  the denominator, all native drains/boost costs and the stat's max-air value. Ring-word-only pokes disappear
  on the next native update; resource tests must change both the fuel and mirror coherently.
- Super-only balance, Riders Crosswake on: Base top/boost/drift 195/270/110, Lv.4 220/300/125 at 200 rings,
  Lv.5 240/330/140 at 300 rings (km/h). Multiply each captured unscaled live field by its target / vanilla
  baseline (top182, boost250, drift100), so existing stat mods retain their proportional changes.
  These are active-block values, not maximum road speeds in every terrain/state. Vanilla Super is unchanged.
- `LvRacer` tracks the block, character/gear identity and each field's baseline/written bits separately.
  A partial stat rebuild cannot compound unchanged fields. Restore only still-owned fields. Clear ownership
  before race initialization and the actual stat constructor `818F2008` (caller `81817150`), so a new gear's
  baseline cannot be confused with the previous gear's numerically identical raised value.
- Explicit applied ownership includes Super's Base tier, where the extended level is zero. Repeated frames
  and tier changes never recapture our own scaled fields; disable/constructor/identity/race resets restore them.
  Only the three speed fields change. Fuel conversion/costs, 999 cap, native level byte, feedback, acceleration,
  animations, board visibility and HUD are unchanged by this small balance update. No larger overhaul.
- Ring-powered racers can skip native level checks. `lv_racers_frame` runs on the game thread at each simulation
  step, maintaining their extension and retiring pending writes on disable/results. Standard racer hooks remain.
- Super feedback enters the existing effect/sound/panel branch at `818360E4`, after all native level, max-speed
  and refill writes. His native level is never synthesized. Sound was muted in automated tests; not listened to.

## Separate digit issue #29

The positioning bug does not select level digits. The old split hooks at `81831148`/`81831178` target the
alternate `fn_1_2FE30` HUD. In a four-player Free Race, all racers reached Lv.5 but those hooks were never called.
The active local HUD is `fn_1_2B3B8`: steady cell pointer `r4` at `8182D528`, change pointer `r3` at `8182D5F8`.
Both now use `lv_hud_cell(..., kind=0)` and the existing generated 20x20 glyphs. This also supplies Super's
multiplayer indication. PC mixed 3P captures show 4 and 5 for Super and normal racers; no issue was closed.

## Local split HUD: confirmed causes and implementation

Local 3P/4P Free Race uses top mode 4; the old edge gate only recognizes mode 8. The renderer previously treated
all 2D draws as a single screen and could combine letters/bands belonging to different players. Perspective
viewports are 640x240 top/bottom in 2P and 320x240 quadrants in 3P/4P. The actual HUD follows in a shared
640x480 orthographic pass, so its active viewport does not establish ownership.

`local_hud_frame` runs after decode on the completed frame. It derives the active layout from perspective
viewports, reuses `ov_bbox` and maps ortho bounds into original full-screen coordinates. Contained draws acquire
an active owner. Shared map/timer bounds reserve their contained markers/letters. Whole-view effects and the
unused 3P silhouette retain coverage. Overlapping/nearby artwork of the same texture and owner forms a group,
including late gauge pieces; adjacent players cannot join. The complete group's margins select its local edge
or centre. Shared/global layers also separate components: the trick-score text after the map overlaps a gauge
by six original pixels and reuses its atlas, but must not join it. Compact runs keep a wider central zone than
tall HUD columns. Centred notifications retain the player's centre; the two sides of 2P remain separate groups.

For original full-screen clip coordinate `x`, view centre `c`, half-width `h` and edge `e` (-1/0/+1), the target is
`x/k + (c + e*h)*(1 - 1/k)`. A copied orthographic projection adds `(k-1)*(c+e*h)` before the existing shader's
division by `k`; a sub-viewport uses the corresponding local coordinate. Every vertex of a group gets the same
translation. Widths/spacing stay original; no new shader/backend path, camera transform, crop, hidden player,
scissor, EFB-copy or guest-memory write. Original 4:3 and online own-view bypass the local pass.

## Reproducible checks and local evidence

`python tests/test_gameplay_hud.py --cc <host-C-compiler>` needs no disc. It compiles the actual gameplay source
and extracts the actual HUD pass/helpers into synthetic full-frame tests. Covers thresholds, repeated updates,
partial stat mods, disable, identity changes, equal-valued new baselines, race reset, all eight racers, fuel ceiling,
resource isolation, shared timer/map, centre notifications, silhouette, clipping and original dimensions.
The frame tests cover 2P/3P/4P at 4:3, 16:9, 16:10, 21:9 and 32:9.

Actual minimized PC deterministic runs, with the port's own PNG captures:

- 20 combinations: 1P/2P/3P/4P x those five aspects. Four captures per case (flyover, start/early race, after
  boost/jump inputs, pause), with RAM confirming race mode/phase. 2P countdown also captured. Screenshots/logs in
  ignored `build/cases/matrix-*`. The final notification-layer refinement has targeted 3P16:9/4P32:9 rechecks;
  the complete source-level aspect matrix also passes after it. Do not call the earlier 20 captures a repeat
  of every layout/phase on the final binary.
- Super at 202/302, near 999 and an over-cap 1050 poke, then below 200 and zero; first block scales/restores,
  native byte stays 0, refill stays 0. Over-cap fuel is clamped; normal consumption can make the later mirror 998.
  Baseline fuel tests collapse to <=100. Plus-off run preserves 182/100/250 and never calls extended HUD/feedback.
- Mixed 3P Super + normal racers: correct digit hooks for both styles, separate per-racer levels, feedback consume
  reports native 0/refill 0 for Super. Poked values and rolling HUD counter can differ temporarily.
- Original 3P vs renderer candidate: full 28 MiB guest RAM byte-identical at presented frame 8500.
- Exact final PC binary: both offline own-view players match baseline on all 6,701 whole-RAM checksum steps,
  the full RAM snapshot and pixel-identical active/pause PNG files. This tests `SR_VIEW`, not live peers.
- Notification refinement: 3P16:9 / 4P32:9 preserve byte-identical guest RAM versus the earlier matrix cases.
  Score/header groups move about their player centre, while main gauges retain full edge moves; shared pieces
  remain unmoved. The added same-atlas/overlapping-gauge synthetic regression passes at all five aspects.
- Local before/after evidence: `build/before-after3p.png`, `build/before-after4p.png`,
  `build/super-progression.png`, `build/super-5-to-base.png`, `build/multiplayer-digits-preview.png`.
  These are local game-derived captures and must remain ignored, never embedded in a commit or uploaded.
- Full Windows and Switch builds; Android ARM64 compilation of the affected runtime sources. Eden v0.1.1 boots
  and renders a 3P race, captured at 5500/6500. All 48 generated/cached Switch DKSH files report scratch 0.
  Emulator timings do not establish hardware performance. No physical gameplay/phone/iOS test in these checks.

The baseline and candidates share existing capped unknown-MMIO read diagnostics; no new explicit runtime fatal
or crash was found in these routes. Inputs, raw logs, RAM dumps, binary hashes and intermediate failed attempts
remain local under `build/`. Intermediate ring-only and single-clamp Super attempts are superseded, not passes.

Changed implementation files: `game_levels.c`, `game_addrs.h`, `frame_pacing.c`, `patches.txt`, `gx_render.c`.
Focused tests: `tests/test_game_levels.c`, `tests/test_local_hud.c`, `tests/test_gameplay_hud.py`. Matching state,
history, changelog, source map, address/gear/roster/debug/wide/online notes and progress documentation are updated.

## Limits before release

### Small balance follow-up (2026-11-18)

Actual-source regressions pass: all ring thresholds and decreases, 100 updates per tier, partial stat mods,
Base ownership/reset/disable, standard-racer controls and all eight racers. A whole-racer comparison permits
only the three speed fields to change during Super's updates. Fuel, ring/native-level bytes and other fields
remain identical. The unchanged actual-source HUD regression matrix also passes; this does not resolve the
new physical 2P report.

Two minimized PC Metal City routes completed six captures each. Plus on snapshots show top/boost/drift
220/300/125 at 202 rings, 240/330/140 at 302 and near999, then 195/270/110 below200 and at zero. Native byte
stays0; log-reader finds exactly two Super feedback events, refill0, no repeated-level spam. Plus off stays
182/250/100 with no extended feedback. Snapshot assertions pass. Both processes were intentionally stopped
after the last capture; manual feel, sound and the new balance on hardware remain untested.

Native local test build `7a05aa6f5f02`, source checkpoint `4459752`, passes NRO0/OUN0 platform/version,
embedded identity, manifest source hashes and SHA-256 sidecar validation. It was then deployed through
OunceMTP with verified stages/installed readbacks and old pair retained locally/onSD. Unlimited/config/env/cache
remain unchanged, saves/unrelated entries preserved. Rollback plan validates against the previous pair.
Eden cannot validate a native Switch 2 binary; no emulator or
physical balance run is claimed. The larger Super overhaul remains deferred.

The returned older hardware log contains repeated Lv.5 bookkeeping at rings548/568; no feedback diagnostic
proves repeated visual effects. Preserve that separate finding for investigation. Normal shutdown completed.
Mode/clocks are incomplete, so screenshot counters/log timings are not qualified hardware benchmarks.

### 2P HUD investigation after deployment

[confirmed] `hud_owner` assigns the global timer to player0 in the halves layout: its entire rectangle is
inside the top half, unlike the central shared map. The shared-reservation pass skips seeds already assigned
to a player, so it does not protect this timer. Its backing overlaps adjacent player artwork by roughly three
source pixels, uses the same atlas, and `hud_join` merges it into player0's component.

Actual PC traces reproduce both states: at6500, all eight timer draws move left with player0; at6250 the
timer stays centred but the adjacent top-only backing stays with it while the ring panel/digits and bottom
counterpart move left. The hardware screenshot shows the more pronounced inward displacement of the top HUD.

Two isolated reproductions using the actual renderer confirm the structural error. With the observed timer
overlap and a tall connected column, the timer projection moves -1/3 instead of0 at16:9. With the rank column
separate, the compact top cluster moves0 instead of-1/3, reproducing inward placement. The group-height
dependent anchor makes the result change with draw/group composition. Prior synthetic tests omitted the
actual overlapping backing, explaining their pass. This is a shared-owner/grouping issue, not native-backend
evidence. The exact hardware draw sequence was not captured, so attribution of every displaced piece remains
[likely]. No HUD fix has been implemented in this investigation.

Next small correction: reserve the shared timer before player grouping in halves, keeping its complete run
separate even when adjacent player artwork overlaps. Verify both timer-centred and whole-player-group edge
invariants at boost/notification states, all aspect layouts and original4:3/online own-view.

### Repeated Lv5 bookkeeping investigation after deployment

[confirmed] The returned old hardware log's1,003 repeated Lv5 events occur at stable548/568 rings, almost
entirely during phaseA (finished), all racer0. It lacks gear/native/feedback diagnostics.

The current code clears ownership via `lv_racers_frame` while finished; `lv_racer_update` lacks that phase
guard and can reapply levels through native hooks. A synthetic actual-source reproduction produces100
clear/reapply cycles,600 speed-field stores (three restore plus three apply per cycle), with feedback pending
rearmed each time. Thus this is redundant stat work, not merely duplicate log output.

An instrumented minimized PC2P route sets a normal racer to568 rings and forces phaseA at6500. BeforeA:
one apply/one feedback; afterA:229 race resets,228 raised-stat applications and228 normal feedback hooks,
all armed with native2. Zero constructor resets occur. The extra unpaired reset is at intentional termination.
The normal feedback hook forces native2 to1 repeatedly; further effect-branch evidence is recorded below.
Temporary source/patch instrumentation and executable remain ignored; reviewed balance source is restored.

A second PC run instruments the actual guest effect branch at818360E4. BeforeA it records three entries
(two native levels, one extended feedback). DuringA it records230 effect-branch entries, matching230 armed
normal-feedback calls and stat reapplications;231 resets include the final unpaired termination reset.
All concern racer0/native2; first fuel199104, then200000 with refill target200000. This directly confirms
repeated native effect-path execution, not just bookkeeping. Visible animation, sound (muted in PC tests)
and individual refill stores were not directly measured. The hardware log lacks that instrumentation, so
the exact hardware effect count is unproven. Temporary probes do not change the installed balance build.

Next small correction: gate stat application and feedback consistently with finished/outside-race ownership
retirement, restore once and stop rearming. Verify regular level-up/down, mods, races/gears and all player
counts. No finished-phase behavior fix has been implemented in this investigation.

Manual play should confirm audible feedback, natural collection to 999, boost/drift feel, damage/falling/capsules,
consecutive race/gear changes and every stage/layout (including players finishing at different times). Automated
tests validate stat/cap paths and selected Metal City routes, not all gameplay or maximum sustainable speeds.
Long-race results, later story scenes/movies, Tag/Survival, real network peers, physical Switch performance,
Android rendering and iOS remain outside this task's actual runtime coverage. Preserve the existing 32:9 speed-blur
outline limitation. Do not treat local tests as user acceptance or permission to publish.


## Current runtime fixes and corrected native icon (2026-11-18)

Implemented public731fd32 / native797d8be: reserve the centred shallow upper clock backing as shared before
player grouping in halves. The existing horizontal-contained shared reservation collects its digits without
joining overlapping P1 art. Halves-only near-gap tolerance joins the shallow boost backing to its ring panel;
quarter tolerances are unchanged. No vertex sizes, guest RAM, scissors/copies, perspective or online view
policy changed. Actual tall/compact overlap tests across all five aspects now pass.

Level application now uses the same deterministic guest mode/phase predicate as frame retirement. Inactive
native hooks restore still-owned fields once and clear pending extension/feedback before either normal or
Super consumption. Active progression/mods/constructor hooks and the latest balance are unchanged.

PC2P16:9/32:9 confirms all13 ring/boost pieces share movement and all eight timer draws stay0. Exact prior
balance3P/4P controls with identical capture schedules have byte-identical fullRAM/active/pause images.
Both online own-view routes retain all6,701 checksum steps, fullRAM and active/pause pixels. These are offline
deterministic controls, not a live-peer hardware test. Original phaseA reproduction now records0 repeated
Lv5 events and0 native effect-branch entries, vs230 before; three ordinary entries occur before finishing.
All temporary probes were removed from tracked/generated source; the clean PC executable was rebuilt.

Native80304ca supersedes the initial launcher icon with setup/icon_256.jpg, the universal game artwork by
cedry2kio. Direct Ninja dependencies, build identity, manifest provenance and exact NRO ASET validation make
it permanent. No conversion is needed. Three focused icon tests pass; artwork bytes are unchanged.
Native0d995f3def88 is installed, with fresh local/SD backups and staged/installed SHA256 readbacks. Unlimited,
settings/env/cache/save bytes are preserved; rollback plan validates. Initial USB loss occurred before any
promotion and was reconciled after user reconnect before a fresh transaction. Exact journal/paths/hashes are
in excluded AI_HANDOFF.md. New physical play/icon acceptance remains pending. No publication.
