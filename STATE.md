# State (2026-11-14, evening)

Read this at the start of every session. Rewrite it in place at the end of each session (never append). Stories
behind each line: `grep` the date, issue number or topic in [docs/HISTORY.md](docs/HISTORY.md). Durable facts
(addresses, `SR_*` variables, procedures): [docs/REFERENCE.md](docs/REFERENCE.md) (index of `docs/notes/`).
Two sessions work on `main`: PC ("Mike") and Mac (iOS); pull before every push.

## Releases

**v0.8.2 released 2026-11-14** ("Sonic Riders Plus", tag on 81ee335): all five zips. iOS zip built on the Mac
2026-11-13 from the tag (VERSION "v0.8.2 (81ee3358)"), cold Mac unzip test PASS (unsigned, fresh tools, 7/7)
[confirmed]; notes' last line points at it. Details: item 0c.

**v0.8.1 released 2026-11-12** (tag on 77620bb; iOS zip replaced 2026-11-13 from abedb6a with the one-app-ID fix).
Heroes alpha call, 999 ring limit, menu L/R tabs + wrap. Cold Sandbox PC + Android and cold Mac PASS [confirmed].

**v0.8.0 released 2026-11-11** (all five zips; replaced in place from be45877, bloom fix + fan icon; cold
Sandbox PC + Android PASS). Picture options on Switch/Android/iOS, fan icon everywhere. iOS: iPad Pro M1 max
settings "flawless" [accepted]; one iPhone frame with a white haze [uncertain]. All four iOS devices on the
standard ID and re-signed 2026-11-13 (profile ends **2026-11-21**: renew before then).
- GitHub lists the 5 newest releases; on each release delete the 6th (tag kept).

## Devices

- S23 Ultra: main APK v0.8.1-25 (5ec23cd), wireless adb at 10.0.0.140 (`adb connect` the mDNS port first).
- OLED (switch-remote): `sonicriders.nro` = c840558 (three swapchain images, cheaper bloom, all 999 gameplay), env.txt
  empty; the user's previous build kept as `sonicriders.user-20261113.nro`, v0.8.0 as `sonicriders.v080.nro`;
  settings 1.5x / Fit window (wide) / Auto / bloom 70 / grading / sharpness 50. Forwarder `SonicRiders.nsp`
  with "Icon 3" in the game folder (user installs it with DBI). Clocks: handheld 1785 / 614 / 2133 (sys-clk
  overlay, never changed), docked 1785 / 1152 / 2133. Two more Switch SD cards on v0.5.3.
- **OLED heavy-view dips fixed on main (2026-11-14, c840558):** three swapchain images; Metal City 1P at the user's
  settings (handheld 1785/614/2133, 1.5x, bloom/grading/sharpness): 4-5k draws 59.9 fps (low 59.9), 5k+ 58.9 (low
  56.2) vs two images 57.3 (low 39.3) / 52.9 (low 33.4) [confirmed, A/B runs]; user: "best run yet", controls the
  same [accepted, that track and settings only]. "GP thread busy" had been mostly waiting for the GPU (real work
  6-8 ms); bloom/grading -1 ms GPU (same picture). HISTORY "2026-11-14 (PC, Mike, later)".

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

0. **iOS: one app ID per player** (released in v0.8.1's iOS zip; notes/ios.md "App IDs"): `<base>.<team>` always;
   `ios_run.py install` moves an older-ID app: A16 iPad, iPhone, iPad Pro (Wi-Fi) [confirmed]; the iPad (7th gen) (Wi-Fi)
   failed after the uninstall (profile lacked the device), restored by hand, save MD5 equal [confirmed]. **To do:**
   `move` should check the device is in the app's embedded.mobileprovision before uninstalling. Not tested: the
   old-ID warning on screen, a move at the 3-app limit, a Sideloadly renewal after a move. Not accepted yet.
1. **"Riders Plus" gameplay set, released in v0.8.2** (the option formerly "Ring Limit set to 999", renamed 2026-11-14 by the user's
   choice; key `ring_cap_999` kept) (notes game-addresses.md "Capsule" / "Object collisions"
   / "Charge jump"). Recordings and online fix the option for the whole run ("ring cap fixed"), so the menu toggle
   does nothing during an SR_RECORD run (by design).
   - **Charge jump grace 0.75 s** (003acfe; user tried 1.5 and 1 s, picked 0.75): the charge slowdown (fn_1_6D5D4)
     and, after a boost, the decay above top speed (store 81815ABC) wait 0.75 s; every racer. PC, the user's
     recorded runs: speed holds/creeps up 0.73 s then the old curve [confirmed, SR_RAMSAMPLE]; user played the
     1 s version ("Perfect") and saw the 0.75 replay ("Perfect amount of hold time") [accepted, PC]. iPad (A16)
     scripted race: same [confirmed]. Not played on Switch/phones by the user; CPU racers not checked.
   - **Gear Lv. 4 / 5** (game_levels.c): Lv. 4 at 200, Lv. 5 at **350** (user: 500 "near impossible"), slowed twice by request
     (2026-11-14: Blue Star 200/275 and 215/300 top/boost; x(200/180, 1.1, 1.1) and x(215/180, 1.2, 1.25)). PC: Lv. 4 at 210, Lv. 5 at 360 rings,
     block values exact [confirmed, log + RAM]. Old 200/500 build played by the user on the OLED [tested, user].
     Not checked: sound, split-screen HUD digit, CPU racers' levels.
   - **Gold 100-ring capsule respawns after 25 s** (7bdce20; user's value): user took it 5 times in one PC race,
     +100 each, >= 26 s apart [confirmed, log]; cap 100 replay: taken once only [likely]. On the OLED, not played.
     Max-air capsule (type 13) takes the same path if it is kind 0x11 [uncertain, no stage checked].
   - **Dash panels**: max(panel speed, own + 25 km/h); **Speed Shoes** (item 6): max(200, own + 80 km/h) (values
     picked by us, user said "you pick"; 2026-11-14). User's PC run (test2.rec): panel 173 -> 200 (original),
     229 -> 255 [confirmed, log]; shoes forced by a test hook in its replay: 198 -> 278, 134 -> 214 [confirmed].
     Kind 0x848 (dash ring?) untouched.
   - Settings-menu details drop-downs: built, then removed 2026-11-14 at the user's request (tooltips only).
0c. **v0.8.2 released 2026-11-14** ("v0.8.2: Sonic Riders Plus", tag on 81ee335; PC, Switch, Android, Linux zips;
   cold Sandbox PC (2 installs, one with a space) + Android PASS [confirmed]; downloaded PC zip = built zip [confirmed,
   hash]). iOS zip: built and uploaded by the Mac session (cold Mac PASS). iPad (A16), build 3ed95db (= tag code):
   Gameplay tab shows "Riders Plus" ticked by default [confirmed, SR_SCREENSHOT]; scripted Metal City Free Race
   (SR_INPUT + SR_RAMSAMPLE, racer 0): A held at cruise keeps the speed flat 45 steps (0.75 s at 60) then the old
   curve (130 -> 122 km/h in 30 steps); held right after a boost, 229 km/h for 45 steps then the decay [confirmed]. Notes: the user's
   ChatGPT banners + our charts + a Lv. 5 PC screenshot pair, all in docs/images/v0.8.2/ (approved by hash in the
   guard); user's rule: **only build zips as release assets**. v0.7.2 delisted (tag kept). Checks before it: OLED
   Splash Canyon / docked / 2P [accepted]; S23 A/B vs v0.8.1 no regression; PC Auto race 59.9 [confirmed].
   **Next focus (user): online play and crossplay**; everything else after v1.
1d. **SwitchWakerHD v1.3.0 ideas** (plans-2026-11.md, end), measured 2026-11-14 on the OLED: tiled cache made frames
   0.6 ms slower (drop it); specialized vertex shaders could save at most ~2 ms (lighting 1.3 + texgen 0.8); PGO /
   core 3 are CPU and the CPU is not the limit at 1P. Left: on-console shader compiles, 30 fps pacing, FXAA,
   anisotropic filtering, PC HDR. Remaining picture-option cost in heavy views: bright pass ~1.5 ms, FSR sharpen ~1.0
   (FSR at 1.5x could sharpen straight from the EFB: saves ~0.3 ms, moves the picture by a pixel; user's call).
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

- [ ] Riders Plus charge jump on the Switch: hold A at cruise and right after a boost (0.75 s hold, then slowdown).
- [ ] Stock clocks with three swapchain images: fps, no new judder.
- [ ] 16:9 wide mode (HUD 4:3) and FOV 120 % in a race: picture, fps vs normal 16:9.
- [ ] Switch-Switch online session (two consoles, `SR_NET` in env.txt, 2P race route): no desync for a whole race.
- [ ] PC-PC session on two PCs (laptop + desktop) over the LAN.
- [ ] `SR_PERFTEST=1` at stock clocks, handheld: save a stock record in `perf/results/`.
- [ ] 4-player Metal City race with `SR_PROFILE=1`: fps, "GP thread busy", game-core idle share vs hw25.
- [ ] Auto mode at stock clocks in a race: listen for crackle; log "mixes started late" and the AX list rate.
- [ ] Story mode: watch one cutscene through and skip one with START; check the monitor (#4).
- [ ] A single sideways Joy-Con as player 1 and as player 2.
- [ ] Copy back `sonicriders.log`, `perftest.json`, `shader_keys.txt` (add new keys to the local key set).
