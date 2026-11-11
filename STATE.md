# State (2026-11-11)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push.

## Releases

**v0.8.0 released 2026-11-11** (tag on 54997c5; all five zips). PC/Switch/Android/Linux zips last replaced
2026-11-11 from be45877 (bloom fix 00d3861 + fan icon as the only default icon); cold Sandbox PC (2 installs, one
with a space) + Android PASS [confirmed]; downloaded PC zip = built zip [confirmed, hash]. iOS zip from 00d3861
(Mac). Contents: picture options (bloom, colour grading, FSR 1, resolution) on Switch, Android, iOS; Linux checked;
finger scrolling in phone menus; cedry2kio's "Icon 3" fan icon on every platform (README credit); bloom only in
races and on the results screen. The version number stayed v0.8.0 (user: few downloads so far), so launchers that
already updated don't see the replacement.
- Switch picture options on hardware [accepted: "60 and pretty", dips in heavy areas; OLED handheld 1785/614/2133].
- iPad Pro 11 M1 on max settings: "flawless" [accepted, user]; iPhone 17 Pro: perf route only (59.94 fps,
  <1 ms per step) [confirmed, logs]; one iPhone frame with options on had a big white haze [uncertain].
- Bloom fix on PC (SR_BLOOM=100 route): Player Entry before and after a race without bloom, race and results with
  bloom [confirmed, frames]; on the iPad [confirmed, user].
- Not tested: a cold Mac test from the iOS zip. The user's free iOS signatures run out ~2026-11-15 (renew; app IDs
  per device in CLAUDE.local.md, untick Sideloadly's "automatic bundle ID").
- GitHub lists the 5 newest releases; on each release delete the 6th (tag kept).

## Devices

- S23 Ultra: v0.8.0-4-g4a24700a APK (installed over USB; wireless debugging was offline).
- OLED (switch-remote): v0.8.0 NRO + forwarder NSP with the first fan icon (not rebuilt for "Icon 3"). Clocks:
  handheld 1785 / 614 / 2133, docked 1785 / 1152 / 2133. Two more Switch SD cards on v0.5.3.

## Platforms

- **Windows** (the fast test loop): whole game incl. story, 1-4P at 60 fps, sound; launcher with updates, remapping
  (accepted), picture options (1x-4x, MSAA, FSR, bloom, colour, smooth lighting), frame-rate presets with in-between
  pictures (render_gl.c pc_fps). Mods layer 1 + stat mods (race effect of gear stats unverified). Launcher icon:
  the user's Super Sonic art (settled); banner (public fan art) still has an open artist check (don't ask the user).
- **Switch**: Homebrew Menu or forwarder NSP; overclocked docked 1P ~60, 3P 55-60 (dips ~48), 4P steady 30; stock
  handheld 30 fps steady, 60 mode drops to 30 in 5,000-7,000-draw views.
- **Android**: S23 Ultra 1P 59.3 / 2P 59.8 fps; touch + pads; 2P local, 3-4P only online. #14 (Adreno 810 stalls):
  fast-core threads + ADPF on main [untested there].
- **iOS**: built on the player's Mac (Mac launcher app, START HERE page); details notes/ios.md.
- **Linux / Steam Deck**: setup_linux.py; gamescope defaults; untested on a real Deck.

## Online play (paused 2026-11-07 at a clean point; docs/ONLINE.md "2026-11-07")

Lockstep with desync check (`SR_NET`) proven PC-PC on localhost (35,400 frames) and PC recording replayed on the
Switch (23847 frames equal). Next: PC-PC over the LAN, Switch-Switch, PC-Switch; player docs.

## Open items, in priority order

1. **Ring cap in races 100 -> 999 (user's top priority, 2026-11-11).** Find the cap check(s), the HUD ring counter
   (3 digits?), and what the ring count feeds (gear level-ups, air, ring-gear boosts, results/rings saved); then a
   setting (off by default) on every platform.
2. **Higher frame rates on iOS and Android** (user): port the PC in-between pictures (render_gl.c pc_fps,
   settings_pc_fps_presets) to render_vk.c; iOS 120 Hz ProMotion (Info.plist CADisableMinimumFrameDuration),
   Android: follow the display refresh rate; phone menu 120 etc. next to 60 / Auto / 30, off by default.
3. MSAA and smooth character lighting on Switch / Android / iOS (notes/picture-options-port.md).
4. In-game update popup (plan first, ask the user).
5. Custom music folder (MP3/OGG/WAV/FLAC -> ADX, per-track toggle; Discussion #17).
6. Online play (above); right-stick camera / look back (notes/plans-2026-11.md); boost camera effect (#16).
7. PC freeze with NVIDIA ShadowPlay recording (2026-11-08): not reproduced [uncertain]; workaround OBS.
8. Open checks: F1 sharpness/pad change survives a restart (2207c58, compile-checked only); Optimus on the gaming
   laptop; bug-report zip with five logs; a real pad with another window focused.
9. Older issues: #1 stock-clock perf, #2 3-4P at 30 fps, #3 speed blur, #4 story monitor, #6 rare loading hang,
   #7 lens flare, #12, #14 (ask the user before posting on #14).

## Wish list / paused

Edge-anchored HUD for ultrawide; HD textures; PS4/PS5 on hold; Vita paused (branch `vita`, notes/vita-research.md).

## Next hardware-test checklist

Console: overclocked unless the test is about stock clocks (say which). Build from current `main`.

- [ ] 16:9 wide mode (HUD 4:3) and FOV 120 % in a race: picture, fps vs normal 16:9.
- [ ] Switch-Switch online session (two consoles, `SR_NET` in env.txt, 2P race route): no desync for a whole race.
- [ ] PC-PC session on two PCs (laptop + desktop) over the LAN.
- [ ] `SR_PERFTEST=1` at stock clocks, handheld: save a stock record in `perf/results/`.
- [ ] 4-player Metal City race with `SR_PROFILE=1`: fps, "GP thread busy", game-core idle share vs hw25.
- [ ] Auto mode at stock clocks in a race: listen for crackle; log "mixes started late" and the AX list rate.
- [ ] Story mode: watch one cutscene through and skip one with START; check the monitor (#4).
- [ ] A single sideways Joy-Con as player 1 and as player 2.
- [ ] Copy back `sonicriders.log`, `perftest.json`, `shader_keys.txt` (add new keys to the local key set).
