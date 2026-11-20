# State (2026-11-20, night)

## Released

- **[v0.9.0 Stable](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0)** (latest, 2026-11-20;
  tag `bce3a5f`): Hold to boost for everyone, Auto spin (#23), same character for several players, Super Sonic
  ring magnet / Ring Replenish / 2.9x held boost to 350 km/h, Cheats tab, Switch sleep + start-up clock fixes,
  unpacked-not-installed check. **Super Sonic flight is an experimental option, off by default** (after an air trick
  he can stay on his board until the next move; cause not found). Zips: PC, Switch, Android, Linux. Cold Sandbox
  PASS: PC (+ space path), Android. iOS v0.9.0 not built yet (needs the Mac); guides point iOS to v0.8.7.
- **[Switch 2 Alpha 3](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.9.0-switch2-alpha.3)**
  (pre-release; tag `67710d8`, branch `release/switch2-alpha3` = local claude/switch2-holdboost + main v0.9.0).
  Cold Sandbox PASS. The user played the test builds before the flight toggle on
  Switch 2 (Race Select audio breakup gone); the final package was not played on hardware.
- Done with the user's OK: "Newer version" lines on v0.8.7 / Alpha 2; v0.8.6 / v0.8.4 release pages deleted (tags kept).
- Installed (readback verified), not yet played: Switch OLED v0.9.0 NRO 4d120ba5..., Switch 2 Alpha 3 bc9dd55883f6.

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

## Unpacked-but-not-installed check (2026-11-19, merged to main, not released)

- Implemented + tested on PC (launcher pictures, game banner capture): when a release ZIP is unzipped over an old
  folder without running the install, the launcher shows FINISH UPDATE instead of PLAY and the game shows a
  banner + F1 > About warning. Not run: FINISH UPDATE through a real install. Ships with the next release.
  docs/notes/launcher.md "Unpacked but not installed".

## Open items (priority order; the user moved online play to the top, 2026-11-19)

0. **Super Sonic flight fix** (off-by-default option in v0.9.0): after an air trick he stays on his board until the
   next action. Log racer 0 animation requests with SR_FLYLOG=1 (runtime/src/super_flight.c), find what the game
   asks for after a trick, fix, then default it on. Also: iOS v0.9.0 zip from the Mac session.

1. **Online play** (Road to v1.0 area, 55 %). Works today: PC-PC, PC-Switch and Switch-Switch lockstep races on a
   home network, two players, PC launcher lobby (docs/ONLINE.md "Lobby in the Windows launcher"). Next, per the
   agreed lobby design (docs/ONLINE.md "Lobby design", not implemented): internet play with 6-character room codes,
   hole punching first + relay fallback (docs/notes/online-research.md), an Online tab in the in-game overlay on
   every platform, joining by restart + catch-up replay. Also left: a room-wide 30 fps switch for slow consoles,
   phones, 3-4 players, ping and delay settings. Code: netplay.c (`SR_NET`), launcher/lobby.cpp, launcher.cpp
   `page_online`. Not started this session.
2. **Hold to boost + Super Sonic package + Auto spin (#23), one release; on hold until the user accepts it all.**
   - Auto spin: merged to main 2026-11-20, not released; user-played on PC (1P + 2P), Switch OLED, S23.
   - Branch `claude/auto-spin` (pushed, **not merged**; Switch 2 copy: local `claude/switch2-holdboost`, never push):
     Hold to boost, Super climb to 350 km/h, Super boost view (full in ~0.4 s), Ring Replenish paid per source
     (grind 70 %, flying 45 % + growth cut, objects as before, spin turn ~1.45 rings, tricks by rank C 15 / B 22 /
     A 30 / AA 33 / S 37 / SS 45 / X 75 rings), Cheats tab ("No ring decrease (Super Sonic)", never online), overlay
     Up/Down wrap with scrolling, clock-gap fix for Switch sleep. **Accepted by the user on Switch 1 (2026-11-20):
     "Super Sonic feels super"**; overlay wrap tested by remote on Switch 1; sleep fix not retested; boost
     muffle sound turned off (user disliked it; SR_BOOSTSOUND=1 keeps it).
   - Before merging: remove the temporary F1 "Super Sonic flight test" section, SR_FORCESUPER hooks, SR_SUPERDIAG;
     pick the flying pose (34) as default; S23 build; release chart of trick rings.
   - **Next (user, 2026-11-20):** (a) let P2-P4 pick the same character (several Super Sonics): find Player Entry's
     "taken" check (docs/notes/roster.md: taken bytes work + 0x20 + id; f_81941560 is the *gear* cursor, not
     characters); (b) keep Super Sonic gold on foot (run-up, standing at the start, out of rings): he is blue
     whenever on foot (state +0x1034 = 1 / 25) while +0xBA stays 11; model choice not found; then board hidden in
     the flying pose.
3. **#33 32-bit Android**: branch `claude/android-armv7` (armeabi-v7a build, setup `-Abi auto`); S23 32-bit
   perftest passed. The reporter's APK stopped with "cannot open the disc image" (no .iso in the app folder; 32-bit
   memory setup worked) [confirmed, crash report]. Plain copy-the-iso steps posted 2026-11-20; waiting for them.
   Recommended code fix (not done): an on-screen message instead of closing, and also accept an .iso in files/.
4. **Switch 2 Race Select audio breakup**: the user reported it gone on the hold-boost test builds (2026-11-20);
   watch for it in Alpha 3 reports. Cause never found.
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

- Installed: Switch 1 ba1b391 (NRO a2a726ea..., env.txt SR_BOOSTLOG=1), Switch 2 49fa438ebf79. Next: several
  Super Sonics in 2P split screen once (a) exists; a long sleep with the game open (sleep fix). Report docked /
  handheld (docked CPU 1785 / GPU 1152 / MEM 2133; handheld CPU 1785 / GPU 614 / MEM 2133).

- Online: a PC-Switch race over the internet once room codes + relay exist (original Switch OLED, overclocked:
  docked CPU 1785 / GPU 1152 / MEM 2133; state the mode and clocks). Online test hardware: gaming laptop + two
  modded Switches.
