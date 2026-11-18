# Riders Plus Super Sonic and local HUD (2026-11-18)

Current: both2P clock and finished-phase defects are now fixed/tested on PC and deployed in0d995f3def88.
The permanent icon is setup/icon_256.jpg (universal game art), superseding launcher conversion. Runtime source
797d8be, icon source80304ca; see STATE.md and docs/notes/switch2-icon.md. Historical checkpoints below predate
these fixes; canonical excluded handoff has current verified installation/rollback.


Latest native follow-up: Super-only Plus active top/boost/drift targets195/270/110 Base,
220/300/125 Lv4 and240/330/140 Lv5. Applied ownership includes Base; ratios preserve stat mods.
Actual-source regressions and minimized PC on/off exact-stat routes pass. Native7a05aa6f5f02 from4459752
is installed via OunceMTP with verified stages/final SHA-256 readbacks, old pair retained locally/onSD,
Unlimited/settings/env/cache preserved and saves/unrelated entries untouched. Hardware balance acceptance
is pending. User's prior-build2P GUI issue and repeatedLv5 records are being investigated separately on the
public local review branch. No corresponding fix is included in this installed balance build. Canonical
excluded handoff holds tooling/worktree paths and easy rollback. Historical pre-balance evidence follows.

Implemented on the local review branch `codex/riders-plus-local-hud`, from public main `bf9f723`.
Not released or user-accepted. Keep source local: no merge, push, release changes or issue replies. The user
subsequently authorized a Switch 2 FTP deployment for a personal playtest; use an isolated native checkout,
back up the installed NRO/manifest, preserve data/settings and verify uploaded readbacks.

## Super Sonic: confirmed causes and implementation

- The old extended-level check required native level 2 and wrote block `+0x94C`. Super Sonic remains at native
  level 0 and uses block `+0x8DC`. Identify him with gear `+0xBB == 18` and transformed character `+0xBA == 11`;
  original character `+0xBF` stays 0. `+0x1034` is an ability mask (5 on both Blue Star and Super), not a gear ID.
- His ring word is a mirror of `floor(fuel * 100 / active max_air)`. The two native fuel clamps at `81815FEC`
  and `818160C8` otherwise limit it to 100. Extend only their ceiling to `max_air * ring_cap / 100`, preserving
  the denominator, all native drains/boost costs and the stat's max-air value. Ring-word-only pokes disappear
  on the next native update; resource tests must change both the fuel and mirror coherently.
- Scale the appropriate live block from its own baseline with the existing multipliers, at 200/300 rings.
  Observed unmodified Super top/drift/boost: 182/100/250; Lv.4 202.222/110/275; Lv.5 217.389/125/300.
  These are block values, not maximum road speeds in every terrain/state. No Faster Super Sonic option.
- `LvRacer` tracks the block, character/gear identity and each field's baseline/written bits separately.
  A partial stat rebuild cannot compound unchanged fields. Restore only still-owned fields. Clear ownership
  before race initialization and the actual stat constructor `818F2008` (caller `81817150`), so a new gear's
  baseline cannot be confused with the previous gear's numerically identical raised value.
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

Personal native Switch 2 follow-up: integrated only the five shared fix sources/tests into an isolated checkout
from the installed personal baseline. Native scheduler/Unlimited regression tests and full pinned-SDK build pass.
Build `da8da376c5f4`, code checkpoint `791de65`, deployed with matching staged/final NRO/manifest readbacks and
verified settings/env preservation. Previous pair backed up locally and on SD; saves/ISO/caches untouched.
Deployment is not physical gameplay validation or acceptance. No source push, merge or publication.

Manual play should confirm audible feedback, natural collection to 999, boost/drift feel, damage/falling/capsules,
consecutive race/gear changes and every stage/layout (including players finishing at different times). Automated
tests validate stat/cap paths and selected Metal City routes, not all gameplay or maximum sustainable speeds.
Long-race results, later story scenes/movies, Tag/Survival, real network peers, physical Switch performance,
Android rendering and iOS remain outside this task's actual runtime coverage. Preserve the existing 32:9 speed-blur
outline limitation. Do not treat local tests as user acceptance or permission to publish.
