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

## Riders Plus website (2026-11-19)

- The user named the project **Riders Plus** (the gameplay changes are the "Riders Plus rules"; repo name kept).
- **Live, approved by the user:** https://teclilla18.github.io/sonic-riders-recomp/ , from the orphan branch
  `gh-pages` (Pages source switched from `main` `/docs`). Releases, downloads and Road to v1.0 update themselves
  from GitHub; the console booklets keep their URLs and are rendered from `tools/console_guide_data.py` (rerun
  `build_site.py all` on gh-pages after changing it). Socials (Bluesky, Instagram) come from `socials.json` on
  gh-pages. Tested in Chrome at desktop and phone widths; not yet on a real phone or Safari. docs/notes/website.md.

## Front page and release pages (2026-11-19, docs only)

- README rewritten: Switch 2 poster first, self-updating Road to v1.0 bar, hook, three buttons, all badges
  together, Riders Plus showcase, one Get it table. All 8 release pages rewritten to one layout. The user
  approved both styles; future README and release changes must follow CLAUDE.md "README style" and
  "Release-notes style".
- `tools/make_progress.py` now draws the top bar, the Status card, the live badge (`docs/progress.json`) and the
  README table numbers from one AREAS list; `.github/workflows/progress.yml` reruns it on push to main
  (first run: success, nothing to change). Overall 77 %, unchanged.

## Open items (priority order; the user moved online play to the top, 2026-11-19)

1. **Online play** (Road to v1.0 area, 55 %). Works today: PC-PC, PC-Switch and Switch-Switch lockstep races on a
   home network, two players, PC launcher lobby (docs/ONLINE.md "Lobby in the Windows launcher"). Next, per the
   agreed lobby design (docs/ONLINE.md "Lobby design", not implemented): internet play with 6-character room codes,
   hole punching first + relay fallback (docs/notes/online-research.md), an Online tab in the in-game overlay on
   every platform, joining by restart + catch-up replay. Also left: a room-wide 30 fps switch for slow consoles,
   phones, 3-4 players, ping and delay settings. Code: netplay.c (`SR_NET`), launcher/lobby.cpp, launcher.cpp
   `page_online`. Not started this session.
2. **Auto stick-spin / air refill (#23)**: approved, plan in docs/notes/plans-2026-11.md "Auto air refill /
   stick spin". Not started. (Turned off in online races if it changes game memory, like the other options.)
3. **#33 32-bit Android**: branch `claude/android-armv7` (armeabi-v7a build, setup `-Abi auto`); S23 32-bit
   perftest passed; test zip posted to #33, waiting for the reporter's result. Not merged.
4. **Switch 2 Race Select audio breakup** (one freeze, then lasting distortion; Unlimited off did not help).
   Cause unknown; whether Alpha 1 had it at the same severity is unproven. Start from the saved log with log-reader.
5. **#6** rare post-race "Now Loading" hang (notes: docs/notes/loading-hang.md). **#29**: fix released in
   v0.8.7; ask the user whether it can be closed. **#31**: Switch Lite update stuck at 0.1.0, no reply yet.
6. **Super Sonic overhaul** (Riders Plus only, after the items above are stable): always flying, golden aura and
   trails, ring magnet, exclusive Lv. 6 Overdrive at 425 rings (275 / 400 / 155 km/h). Plan only.
7. **Switch texture-pack first-appearance freezes** (up to ~1.3 s; set aside by the user 2026-11-19). Plan when
   picked up: decode replacements on a worker and swap them in (texpack.c, gx_texture.c); baseline log
   `build/old/switch-texpack-20261116.log`.
8. History-only CI failure (old banner blobs rejected by the "every commit" guard step): ask the user before
   changing the history check.

## Next hardware test

- Online: a PC-Switch race over the internet once room codes + relay exist (original Switch OLED, overclocked:
  docked CPU 1785 / GPU 1152 / MEM 2133; state the mode and clocks). Online test hardware: gaming laptop + two
  modded Switches.
