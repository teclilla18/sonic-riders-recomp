# State (2026-11-19)

## Released

- **[v0.8.7 Stable](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7)** (latest; tag
  `381a369`): Windows, original Switch, Android, Linux and iOS setup ZIPs. Super Sonic in Riders Plus (999 rings,
  Lv. 4/5; top/boost/drift Base 195/270/110, Lv. 4 220/300/125, Lv. 5 240/330/140 km/h), adaptive split-screen
  HUD, multiplayer Lv. 4/5 digits (#29), no repeated finished-race level-ups, Android Vulkan 1.1 + quit fixes
  (#32), Linux setup fix (#30). Tested: PC matrix and both own-view checksum routes, cold Sandbox PC + Android,
  cold Mac + iPhone 17 Pro race. iPads not retested.
- **[Switch 2 Alpha 2](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7-switch2-alpha.2)**
  (pre-release; tag `1ca54eb`, runtime = accepted build `0d995f3def88`). **Accepted by the user on Switch 2:**
  icon, Super Sonic feel, high-ring finish, 2P Fit HUD. Fresh installs default to 60 FPS; Unlimited experimental.
- Release downloads are setup ZIPs only (rule in CLAUDE.md); both alphas' `.sha256` files were removed.

## Front page and release pages (2026-11-19, docs only)

- README rewritten: Switch 2 poster first, self-updating Road to v1.0 bar, hook, three buttons, all badges
  together, Riders Plus showcase, one Get it table. All 8 release pages rewritten to one layout. The user
  approved both styles; future README and release changes must follow CLAUDE.md "README style" and
  "Release-notes style".
- `tools/make_progress.py` now draws the top bar, the Status card, the live badge (`docs/progress.json`) and the
  README table numbers from one AREAS list; `.github/workflows/progress.yml` reruns it on push to main
  (first run: success, nothing to change). Overall 77 %, unchanged.

## Open items (priority order, from the user's plan)

1. **Switch texture-pack freezes** (promised for a release, overdue): the game freezes up to ~1.3 s the first
   time each HD texture appears. Plan: draw the game's own texture first, decode the replacement on a worker,
   swap it in when ready (`runtime/src/texpack.c` texpack_load / load_dds, `gx_texture.c` cache miss around
   gx_texture_get). Baseline log: `build/old/switch-texpack-20261116.log` (65 `[hitch]` lines with "lookups").
   Not started.
2. **Auto stick-spin / air refill (#23)**: approved, plan in docs/notes/plans-2026-11.md "Auto air refill /
   stick spin". Not started.
3. **#33 32-bit Android**: branch `claude/android-armv7` (armeabi-v7a build, setup `-Abi auto`); S23 32-bit
   perftest passed; test zip posted to #33, waiting for the reporter's result. Not merged.
4. **Switch 2 Race Select audio breakup** (one freeze, then lasting distortion; Unlimited off did not help).
   Cause unknown; whether Alpha 1 had it at the same severity is unproven. Start from the saved log with log-reader.
5. **#6** rare post-race "Now Loading" hang (notes: docs/notes/loading-hang.md). **#29**: fix released in
   v0.8.7; ask the user whether it can be closed. **#31**: Switch Lite update stuck at 0.1.0, no reply yet.
6. **Super Sonic overhaul** (Riders Plus only, after 1-4 are stable): always flying, golden aura and trails,
   ring magnet, exclusive Lv. 6 Overdrive at 425 rings (275 / 400 / 155 km/h). Plan only; nothing implemented.
7. History-only CI failure (old banner blobs rejected by the "every commit" guard step): the Heroes fix (PAST_ART
   for earlier versions) is partly in; ask the user before changing the history check.

## Next hardware test

- Original Switch OLED (overclocked: docked CPU 1785 / GPU 1152 / MEM 2133): HD texture pack with background
  loading, full pack already in `mods/HDPackTest` on the SD card; compare `[hitch]` lines with the baseline.
