# Changelog

## Unreleased

- **Up to 999 rings in a race** (new default, every mode: Normal Race, World Grand Prix, Story, Tag, 1-4 players).
  A hit from an opponent's boost now takes 100 rings instead of all of them. The original game stops at 100 and
  loses everything; settings menu > Gameplay > "Ring Limit set to 999" (untick) brings that back. The max-level
  item box no longer lowers a count above 100.
- The "check for updates" switch (F1 > About) is now remembered after a restart.
- **Windows launcher:** the icon step now says the fan icon by cedry2kio is used unless you pick your own picture
  (it still named the old memory card picture; the icon itself was already right).

## v0.8.0 (2026-11-11)

- **Picture options on Switch, Android and Linux / Steam Deck.** The settings menu's Display tab now has the PC's
  picture options on more platforms, all off by default so nothing gets slower unless you turn them on:
  - **Switch:** Sharpness (AMD FSR 1), Bloom, Colour (Original / Vivid / Warm / Cool and sliders), next to the
    1x / 1.5x / 2x resolution. On an overclocked OLED (handheld) a 1x race with all three on still runs at 60 fps;
    2x with them suits the 30 fps mode better.
  - **Android:** the same three, plus the 1x / 1.5x / 2x resolution choice (new on phones).
  - **Linux / Steam Deck:** already had everything the Windows version has (resolution 1x-4x, anti-aliasing,
    sharpness, bloom, colour, smooth character lighting); checked for this release.
  - **iPhone / iPad:** the Android set (resolution, Sharpness, Bloom, Colour), all off by default.
  - Not yet on Switch, Android, iPhone and iPad: anti-aliasing and smooth character lighting.
- **Bloom stays off in menus.** With Bloom turned on, character select after a race could glow almost white; bloom
  now shows only in races and on the results screen.
- **New game icon** on every platform: a fan-made Sonic Riders icon by cedry2kio (DeviantArt). The installers use it
  unless you pick your own picture (the game's save icon is no longer used).

## v0.7.4 (2026-11-10)

- **iPhone and iPad: no more second app when renewing.** The Mac installer builds with the ID of the Sonic Riders
  app already on your device, and new installs use the ID Sideloadly makes with its default settings, so renewing
  either way updates the same app (saves and disc image stay). If you already have two, INSTALL.md shows how to
  move your save across in the Files app.
- **iPhone and iPad: controllers.** With an Xbox or PlayStation pad, A / Cross paused the race instead of jumping
  (iPadOS also sends pad buttons as key presses). The keyboard is ignored while a controller is connected.

## v0.7.3 (2026-11-09)

- **Update from the launcher works now.** After DOWNLOAD AND INSTALL and a restart, the launcher still showed the old
  version and offered the update again: the copy step skipped files that had the same size as before (the version
  file among them). Every file is copied now, and the release zips carry their own date, so launchers from v0.7.1 and
  v0.7.2 also update correctly to this version.

## v0.7.2 (2026-11-09)

- **iPhone and iPad: Mac launcher.** The iOS zip now has *Sonic Riders Launcher*, an app like the PC launcher:
  it checks and installs what the build needs (Xcode, Metal Toolchain, Homebrew tools) with one button each,
  builds the app with a progress bar, walks you through Sideloadly, Developer Mode and trusting your Apple ID, and
  shows when the 7 days run out (with a Calendar reminder). *START HERE (Mac).html* shows the one-time macOS
  "Open Anyway" steps with pictures of the real dialogs. **Your own app icon:** pick any picture (PNG, JPG, HEIC, WEBP, GIF, BMP, TIFF).

## v0.7.1 (2026-11-09; zips updated the same day)

- **Update from the launcher.** When a new version is out, the launcher and the Switch / Android installers show
  an Update page with its release notes and a DOWNLOAD AND INSTALL button: it downloads the new version and puts
  it over your folder (disc image, saves, mods and settings stay), then restarts. On PC, press Rebuild on the Play
  page afterwards. The game's F1 > About shows the release notes too.
- **Switch installer: choosing an SD card works.** A drive like `F:\` stopped the build at step 2 ("Illegal
  characters in path"): the path reached the setup with a stray quote.
- **Launcher and installers: bigger text** and a larger window (it still fits smaller screens).
- Also new since v0.7.0's first upload: the Switch / Android setup window, the update notice and the launcher icon
  (below).

## v0.7.0 (2026-11-08; zips updated 2026-11-09)

- **Switch and Android: a setup window (added to the zips on 2026-11-09) instead of a command window.** `Install Sonic Riders (Switch).exe` and
  `Install Sonic Riders (Android).exe` look like the PC launcher: pick your disc image, your SD card (found by
  itself) or phone, your own icon and the Switch HOME-screen icon, then BUILD AND INSTALL with a progress bar and
  the setup's messages. The command-window setup stays as `... - command line.bat`.
- **PC: update notice** (added to the v0.7.0 PC zip on 2026-11-09): the launcher and the game check GitHub at
  start-up and say when a newer version is out (a gold "Download update" button in the launcher, a message at the
  top of the game screen, and F1 > About with a button to the download page). Only the newest version number is
  downloaded. Off with F1 > About > "Check for updates at start-up".
- **PC: new launcher icon** (Super Sonic; PC zip updated 2026-11-09).
- **iPhone and iPad (new):** `SonicRiders-iOS-Setup-<version>.zip` builds the app on your Mac (Apple silicon,
  Xcode) from your own disc image and installs it on a connected iPhone/iPad, or makes an `.ipa` for Sideloadly /
  AltStore. Races at 60 fps on iPhone 17 Pro, iPad Pro 11" (M1) and iPad (A16); iPad 7th gen with small dips.
  Touch controls sized for fingers on phones and tablets, game controllers, the game pauses when you lock the
  device or open Control Center, and the app tells you when the disc image is missing or still copying.
- **PC: FSR sharpening** (AMD FidelityFX Super Resolution 1) in F1 > Display and the launcher's Graphics page: a
  Sharpness slider, 0-100% (Off by default). When the window is bigger than the game's resolution, FSR also
  does the upscaling instead of a plain stretch, so 1x-2x resolution looks sharper on big screens.
- **PC: controllers work in the background** (F1 > Controls, launcher Controls page; on by default): a
  controller keeps playing while another window has focus.
- **PC: laptops with two graphics chips** (NVIDIA Optimus, AMD switchable) now run the game on the dedicated one.
- **PC and Linux: the last five logs are kept** (`sonicriders.log`, `.prev.log`, `.prev2.log` to `.prev4.log`),
  all in the bug-report zip.
- **PC: frame rate presets** in F1 > Performance and the launcher: 30, 45, 60, 120, 144, 180, 240 or Unlimited.
  The game still runs at its own speed; above 60 the PC draws extra pictures in between for smoother motion.
  High rates may need a lower resolution or less anti-aliasing.
- **PC: bloom only in races.** Menus and character select stay as drawn (characters there looked almost
  white with bloom on).
- **Android: no more "game crashed" report after closing the app.** Closing the game (swiping it away) could
  crash it on the way out, so the next start showed a crash report.

## v0.6.0 (2026-11-07)

- **PC: smooth (per-pixel) character lighting** in F1 > Display > Character lighting: shading and highlights on
  characters, boards and other lit models worked out per pixel instead of per vertex. Off by default.
- **PC: bloom and colour grading** in F1 > Display: a Bloom slider (soft glow around bright parts) and Colour
  presets (Original, Vivid, Warm, Cool) with brightness, contrast, saturation and warmth sliders. Only the 3D
  picture changes; the HUD and menus stay as drawn. Off by default.
- **iOS: in progress, not in this release.** It plays on an iPhone already; a Mac setup for players comes in a
  later release (an iOS app can't be offered as a download: everyone builds it from their own disc).
- **PC: internal resolution 1x-4x and anti-aliasing (2x / 4x / 8x MSAA)** in F1 > Display. The default stays
  2x without anti-aliasing (the picture as before); 3x suits 1440p screens, 4x suits 4K.
- **"Save screenshot" on Switch and Android no longer captures the settings menu** (it did when the FPS counter
  was on).
- **Android: the game's threads run on the fast CPU cores at a higher priority**, and the game tells Android how
  much work each frame needs (performance hint), against the short stalls some phones showed (#14). No change on a
  Galaxy S23 Ultra, which never stalled; untested on an affected phone.
- **Windows launcher: SonicRidersLauncher.exe in the PC zip.** Pick your disc and press INSTALL AND PLAY; then
  PLAY / Stop, the settings (shared with the in-game F1 menu), mods, and **button remapping** (Controls page:
  click a box and press a key or controller button). Banner: fan art chosen by the maintainer.
- **PC: keys and controller buttons can be remapped** (`saves\controls.ini`, written by the launcher). Without the
  file the buttons are as before. The F1 menu's Controls tab shows the buttons in use.
- Online play groundwork: a slow disc read (e.g. from a slow SD card) could make two machines' games drift apart.

## v0.5.3 (2026-11-07)

- **Track Select in Fit window / 16:9 / ultrawide:** the row of course pictures was torn apart (the outer pictures
  pulled out of their frame toward the screen edges). It now stays together like on GameCube; the menu bars still
  reach the screen edges.

## v0.5.2 (2026-11-07)

- **Switch races are fast again (~60 fps overclocked).** Builds made from the release zips had none of the
  fast "specialized" graphics shaders: they are built from a list of the game's graphics-chip settings that
  was only on the developer's PC, so every release build (v0.4.x and v0.5.x alike) drew everything with the
  slow general shader, 30-45 ms of GPU time per race frame. The list now ships with the source
  (`config/GXEE8P/shader_keys.txt`), so your build has all of them. The log line
  `[deko3d] 32 specialized fragment shaders` shows it worked (it said 0 before).
- Android builds from the zip get the same shaders (on a test phone they took the 1P demo race from 55 to 59 fps).

## v0.5.1 (2026-11-07; v0.5.0 was pulled, its tag kept)

- **Known issue: the Switch build is slower than v0.4.2** (user's test: under 30 fps even overclocked, any
  resolution or screen mode). Switch players are told to stay on v0.4.2 until it is fixed.

- **v0.5.0 was pulled by the user** for its ultrawide bugs. Fixed since (PC, Fit window at 21:9):
  - menus, the Extras screens and movie subtitles after story mode had their text torn to the screen edges
    (the race-only HUD placement stayed on); the race's quit dialog too;
  - the dark bars of the menus now reach the screen edges;
  - the race results screen (ranks, names, times) stays together in the middle instead of being torn to
    the edges;
  - movies (intro, story scenes, Theater) keep their 4:3 shape with black bars instead of being stretched
    (16:9 widescreen and Fit window; "stretch HUD and menus" still stretches them);
  - "Save screenshot" no longer captures the settings menu itself (PC, Switch, Android).
- **Switch HOME-screen icon**: the setup can make a forwarder `.nsp` (asks up front; needs your console's
  `prod.keys`; optional picture of your own, 256 x 256) and copies it to `NSP\` on the SD card. Install it once
  with DBI or Goldleaf (docs/INSTALL.md).
- Your own icon picture may now be .png/.jpg/.bmp for the PC build too (made into an .ico).
- **Chaos Emerald option: off by default** and renamed "Chaos Emerald gear in the Shop for 0 rings" (buy it in
  the Shop, then pick it for Sonic). The setting starts off again for everyone once.
- **Steam Deck / gamescope**: the window opens at the screen's size and the first run defaults to fullscreen
  and Fit window (16:10, no 4:3 bars).
- **Save a bug report** (settings menu > About; PC, Linux, Steam Deck): one zip with both logs, setup.log,
  settings and system info in the game's folder, which opens.

## v0.5.0 (2026-11-06)

- **Ultrawide without stretching, and a field of view setting** (Discussions #15; settings menu > Display).
  Three screen modes:
  - **4:3** (original);
  - **16:9 widescreen**: the game draws a wider picture instead of stretching; HUD and menus keep their 4:3 shape
    in the middle (option: stretch them to the width);
  - **Fit window**: matches your window or screen (16:10 Steam Deck, 21:9 and 32:9 ultrawide, ~20:9 phones). In
    races the HUD moves to the screen edges; menus stay in the middle with their backgrounds filling the screen.
  - The old "16:9 stretched" mode is gone (a saved setting becomes 16:9 widescreen).
  - Field of view 80-130%.
- Fixed in the wide modes: a faint copy ("mirage") of your character to its right (the speed-blur effect was
  squeezed), a smeared colour gradient at the bottom left of menus, and gaps in the title screen's bands.
- PC: the settings menu (F1) works with the mouse: point, click, drag sliders, scroll. The cursor shows while the
  menu is open, also in fullscreen.
- Online play groundwork (not a feature yet): a session uses the host's picture shape on every machine (it is
  part of the game's state), timing fallbacks no longer act during a session, and the PC now makes the same "NaN"
  float values as the Switch and phones (needed for PC-Switch sessions; not tested between machines yet).
- Android setup: a phone connected by USB and by wireless debugging at the same time counted as two devices,
  so the setup refused to install; it now counts it once and installs over USB.

- **Linux and Steam Deck (new; first Deck report on gbatemp: it works):** `SonicRiders-Linux-Setup-<version>.zip` with
  `Install Sonic Riders (Linux).sh`. It builds the PC version on the Linux machine itself from your disc image,
  with nothing to install and no root rights: system Python 3, SDL2 and OpenGL (SteamOS has them), and its own
  downloaded tools, including the zig compiler when the system has no clang or gcc. It adds a menu entry, a
  launcher, and says how to add the game to Steam for Game Mode. Tested on Ubuntu 24.04 (WSL), with and without
  a system compiler. ([Install on Linux / Steam Deck](docs/INSTALL.md#install-on-linux--steam-deck))

- Android: when the phone's graphics driver resets (Vulkan "device lost"), the game now rebuilds its renderer and
  carries on after a pause of about a third of a second, instead of closing. While the screen is being recorded on
  the Galaxy S23 Ultra the reset still repeats every few frames in races, and after the fourth one within 10 s the
  game closes with a crash report as before. Screen recording on Android is therefore not supported (README).

## v0.4.2 (2026-11-05)

- Android: crash reports you can send from the phone. When the game crashed or stopped with an error last time,
  the next launch saves a report (the game's log, the crash and the phone model) to
  `Download/SonicRiders/sonicriders-crash-<time>.txt` and offers **Share report**. Any file manager can open that
  folder (the game's own folder under `Android/data` is hidden from them since Android 11). Native crashes are
  caught by a new crash handler, like the PC's `[crash]` lines.

## v0.4.1 (2026-11-04)

- Android setup: when no phone was found (or it had not allowed USB debugging yet), the install step was
  skipped without a word. It now says why and what to do (issue #14; the v0.4.1 Android zip was updated
  in place on 2026-11-05).
- Android setup: a first build could fail at the very end with "unable to find library -lSDL2" (the game was
  linked before SDL2 was built; running the setup again worked). The link now waits for SDL2 (issue #13).
- Android setup from the release download failed at the last step ("unrecognized arguments") for everyone in
  v0.4.0; fixed. An Android-only setup on a fresh PC also stopped at "Dear ImGui not found"; fixed.
- Setup on a fresh Windows (nothing installed): the code analysis step failed (a missing Visual C++ runtime
  DLL); the setup now brings its own copy.
- Setup downloads continue after a dropped connection (up to 5 tries, resuming) instead of stopping.
- Android setup no longer leaves a background `adb.exe` running.
- The "Install Sonic Riders" .bat returns the setup's result, so it can be scripted. Releases are now tested by
  a clean install in Windows Sandbox (PC and Android).

## v0.4.0 (2026-11-04)

- **New: Android phones** (64-bit, Android 11 or newer): a new `SonicRiders-Android-Setup` download builds the
  game on your PC from your disc image and installs it on your phone over USB (docs/INSTALL.md, "Install on
  Android"). Tested on a Galaxy S23 Ultra (60 fps races).
- **Known issue (Android):** the game can crash while you record the screen (rarely also when the phone is very
  warm): the phone's graphics driver resets it. Reopen the game; a fix is being worked on.
- Android port (docs/ANDROID_PLAN.md): the setup has a new `-Target android` that
  downloads the Android tools (pinned and checked), builds the game for your phone, installs it and copies your disc
  image to it over USB. On Android the game now draws with Vulkan (first picture: the intro movie), with touch
  controls (stick on the left; jump, boost, drift and tornado on the right; pause and menu buttons in the top corners)
  and the screen kept awake while playing.
- Android: the app has an icon (made from the game's save icon on your disc, or your own picture with the setup's
  `-Icon`); the touch PAUSE button says START; the touch stick sits a little higher and is calmer in menus; the
  settings menu works reliably by touch (Close really closes it) and its Controls tab explains touch and controllers.
- Android: races are faster on the phone's graphics chip (the S23 Ultra's demo races now run at a steady 59-60
  fps), using the same prepared shaders as the Switch.
- Controllers (PC and Android): analog triggers adapt to each controller, so a full press always clicks L / R
  (drift) like a GameCube trigger, while a partial press stays analog.
- Perf test with `SR_DETCLOCK`: the intro skip now lands on the same frame on every machine (runs can be compared).
- Groundwork for online play (test options for now, see docs/ONLINE.md): record a session's controller
  input and replay it exactly (`SR_RECORD`, `SR_REPLAY`; a recording made on PC replays identically on
  the Switch), and two copies of the game racing in lockstep over the network (`SR_NET`), with a
  "desync" message if the two games ever stop matching.
- Online play (still test options): every machine uses a generated save with everything unlocked (all
  characters, tracks and gear), built in memory; your own memory card is never read or written during an
  online session. In a two-player race each machine shows only its own player's view. The first session
  between a PC and a Switch ran in lockstep (a known difference between PC and Switch still ends cross-play
  races; PC-PC and Switch-Switch are not affected).
- Performance test for contributors (docs/PERF.md): `SR_PERFTEST=1` measures the title screen's two demo
  races the same way every time, on PC or Switch, without touching your save; `tools/perf_test.py`
  compares the result with a saved record.

## v0.3.4 (2026-10-29)

- **Setup: works when your Windows user name has accents or other non-English letters.** The build
  tools lost those letters in paths ("CreateProcess failed: The system cannot find the file
  specified" for every tool). The tools folder now moves to `C:\SonicRidersTools` in that case, build
  paths are kept to plain letters, and if the folder you extracted the zip to has such letters the
  setup says so at the start (extract it to e.g. `C:\SonicRiders` instead).

## v0.3.3 (2026-10-29)

- **Switch: characters are no longer tinted blue/teal (#11).** Lit 3D models lost most of their red
  on the Switch only: white gloves and shoes came out cyan, skin olive, in menus and in every race.
  The Switch's shader compiler mishandled the lighting code; it is restructured so the lighting
  now matches the GameCube (checked on hardware against the original game in Dolphin).

## v0.3.2 (2026-10-26)

- **Switch: fixed a large slowdown after the first few minutes.** After some races the game leaves a
  rarely used color setting switched on, and from then on the port stopped using its fast
  precompiled shaders for everything: menus and races took several times longer on the GPU (about
  20 fps at stock clocks). The fast shaders now keep working for the whole session.
- Faster translated game code on Switch: cheaper function calls and memory accesses (7% smaller code).

## v0.3.1

- **Switch: playable without an overclock.** At stock clocks, races now hold a steady 30 fps (tested
  on hardware in handheld mode, including the busiest views). Two changes made this possible:
  - The vertex shader reads the game's matrices and lights from fast GPU memory: about 27% less GPU
    time per race frame on hardware (e.g. 15 ms down to 11 ms).
  - The game's vertices are converted on a second CPU core that was otherwise waiting for the GPU:
    the thread that turns the game's drawing commands into GPU work went from 18-19 ms to 8-13 ms
    per frame in busy races.
- **The "60 fps" setting no longer slows the game down.** It now draws 60 fps while the console
  keeps up and switches to a steady 30 fps at full speed in scenes too busy for 60, going back to 60
  when the scene allows it. On a Switch at stock clocks, light parts of a race run at 60 and the
  busiest ones at 30.
- The Switch menu has no Quit Game button (close the game from HOME).
- The FPS counter alone no longer blends a full-screen layer each frame (less GPU work), and the
  menu is drawn on a helper thread (no 30 ms hitches when moving the cursor on the Switch).
- 30 fps chosen in the middle of a race now also runs effects at the right speed straight away.
- The log records every settings change and the GPU's time per frame, for hardware reports.
- **New settings menu** in the style of SwitchWakerHD's, in Sonic colours: tabs (Display,
  Performance, Controls, Gameplay, Mods, About) switched with L / R, a gold cursor, a short
  explanation of the selected option, the Switch's own font, and (on PC) a Quit Game button that asks first.
- **No more slow motion when the game is busy.** New setting **Frame rate**: *Auto (keep speed)*,
  the default, draws 60 fps while it can and, when a frame takes too long, leaves out drawing a
  frame instead of slowing the whole game down, so races always run at their real speed (the game
  keeps 60 steps a second; you see fewer frames, e.g. 35-55 fps, instead of 30 fps in slow motion).
  *30 fps (full speed)* uses Sonic Riders' own unused 30 fps mode: a steady 30 fps at full speed.
  *60 fps* (originally the game's own behaviour, slow motion when late; changed in v0.3.1, see above).
- **Faster game code** on both platforms (most on the Switch): the translated game code is about
  30% smaller on the Switch and does less work per instruction (ideas from SwitchWakerHD); the intro
  movie's video decoding is much cheaper. Pictures are unchanged.
- Switch: fixed crackling sound in busy races at stock clocks (the sound mixer now gets the CPU before
  the renderer; not yet confirmed on hardware). Slightly less work per frame for the renderer.
- Switch: in handheld mode the GPU uses Nintendo's own 384 MHz setting for games instead of the
  system's 307 MHz (not an overclock; docked is unchanged).
- **PC: file mods.** A `mods` folder next to the game replaces or adds the game's files (character
  models, textures, music, stages) without touching your disc image; `mods/mods.ini` sets the order
  and turns mods off; the settings menu has a **Mods** page to turn them on and off. See
  [docs/MODDING.md](docs/MODDING.md).
- **PC: stat mods.** A mod's `mod.ini` can change Extreme Gear stats by name (`[gear.HighBooster]`
  `level1_boost_speed_kmh = 260`), write memory, and run simple Gecko codes. New tracks are planned.
- The README is shorter; installation, playing, bug reports, building and modding have their own
  pages in `docs/`.
- **The picture is no longer dark and blue-tinted (#11), and the speed blur shows (#3).** The game's
  speed-blur effect draws the previous frame over the screen edges; the port fed it the wrong
  picture (old memory), which darkened and tinted the whole screen, characters included. Races now
  look like they do on a GameCube.
- **Distance fog** is drawn (it was missing in every race).
- **Characters have their shine back (#11).** Characters looked flat and dark blue because their
  specular highlights were never drawn (a light at position 0,0,0 made the lighting maths divide by
  zero). Sonic's quills and the other characters now get the light-blue highlights the GameCube shows.

## v0.2.4 (2026-10-23)

- PC: the settings menu no longer shows the "Controllers" row; it only did something on the
  Switch (PC controllers are picked up automatically).
- PC: controller sticks are much less touchy in menus. A light touch on an Xbox-style stick used
  to send the cursor racing through menus; the stick now has a round dead zone (25%) and a smooth
  ramp after it, and a full push still steers fully.
- Switch: **sleep mode works.** Putting the console to sleep during the game and waking it used to
  leave the game frozen; it now carries on where it was (confirmed on hardware).
- **Faster multiplayer on the Switch:** the game's audio is now mixed on its own thread, as the
  GameCube's audio chip does (it took about 2 ms of every frame from the game). Busy parts of 3-player
  Metal City went from about 37 to around 50 fps (mostly 60 elsewhere). The graphics command path
  also no longer makes two CPU cores fight over the same memory (a small gain). 4 players not
  measured yet.

## v0.2.3 (2026-10-23)

- **PC: game controllers work now.** Controllers were detected but none of their buttons or sticks
  reached the game (only the keyboard worked). Analog triggers work too.
- The settings menu opens when F1 (or the controller's Back/View button) is **held** for half a
  second; the docs said "press".
- The PC log records the first controller button presses, to help with bug reports.
- **PC: the program has an icon** (Sonic, the game's own save icon), made from your disc during
  setup, for the program, its shortcuts, the window and the taskbar.

## v0.2.2 (2026-10-23)

- **Setup fix:** on some PCs the setup stopped with `Error opening archive: Can't initialize filter;
  unable to run program "xz -d -qq"`. Windows' built-in `tar` can't unpack some of the downloaded
  tools on many Windows versions; the setup now unpacks them itself and needs no other programs.
- README: the minimum Windows version and requirements are now at the top.

## v0.2.1 (2026-10-23)

- **PC keyboard controls changed:** WASD moves, J / K / L / I = A / B / X / Y, U / O / P = L / R / Z,
  Enter = START, arrow keys = C-stick, T / F / G / H = D-pad. The settings menu (F1) also takes
  WASD, J (accept) and K (back).
- The v0.2.0 palette fix is now confirmed on Switch hardware: trick rank letters, track select
  previews, the results screen, the pause and end-of-race menus all have the right colours, the white
  boxes around results icons are gone, and the 4-player UI no longer flickers.

## v0.2.0 (2026-10-22)

### New: play on Windows PC
- A separate **PC download** (`SonicRiders-PC-Setup`): builds a native Windows version from your own
  disc image, no emulator. The Switch download is now `SonicRiders-Switch-Setup`.
- The PC version is a normal windowed program (no console window; its log is `sonicriders.log` in the
  install folder), with vsync, borderless fullscreen (F11 / Alt+Enter, or the settings menu), a
  "Sonic Riders" shortcut in the install folder and optionally on the desktop.

### Fixes and improvements

- Setup: the game code is recompiled once per run, not twice; re-running the setup with nothing
  changed finishes in seconds.
- Fixed the rainbow-coloured trick rank letters (and other UI textures with the wrong palette):
  palette addresses are now masked to physical memory like the hardware does.
- Palettes are captured when the game loads them, not later on the graphics thread.
- Faster vertex decoding on the GP thread (about 40% less decode time, identical results): a step
  towards 60 fps in 3-4 player races.
- Quieter recompiler output: one summary line; details (unhandled data words, FPSCR notes) go to
  `build/gen/report.txt`.

## v0.1.0 (2026-10-22)

First public version. Everything below is built from the user's own disc image; no game data is
distributed.

### Recompilation and boot
- PowerPC → C static recompiler (forked from ZeldaWWHDRecomp's translator) for `main.dol` and
  `_Main.rel`, driven by decomp-toolkit's analysis; code patches in `config/GXEE8P/patches.txt`.
- The game's own OS, scheduler, alarms and SDK libraries run recompiled; only the thread switch,
  REL linking and a few hardware-only routines are native.
- Register-level models of VI, DI (disc), CP/PE/PI (graphics FIFO), DSP/ARAM/AI, EXI (memory card,
  IPL SRAM/RTC) and SI (controllers).
- Disc reads arrive progressively during the transfer, as on hardware (fixes the 4-player stage-load
  crash; the game's streaming decompressor relies on it).
- Story-mode crash fixed (interrupt handlers no longer run below the thread's stack).

### Graphics
- deko3d renderer (Switch) and OpenGL 3.3 renderer (PC) sharing one GX ubershader; TEV, indirect
  texturing, EFB copies, destination alpha with dual-source blending (fixes see-through surfaces).
- Ahead-of-time specialized fragment shaders (deko3d) from recorded graphics-state keys.
- GX processing on its own thread; indexed draws; pipelined frames (CPU/GPU overlap).
- Texture cache (two-way set, palette-aware keys).
- 16:9 widescreen (native form of the known Gecko code), 4:3, stretched; resolution 1x/1.5x/2x.

### Input and multiplayer
- Up to 4 players: Pro Controller, Joy-Con pair, handheld, single sideways Joy-Con; the system
  controller applet from the settings menu. PC: keyboard and up to 4 gamepads.

### Audio
- Native AX DSP microcode (ADPCM/PCM from ARAM, resampling, envelopes, filters, aux buses) with
  audout output on the Switch and SDL on PC.

### Saves and settings
- Memory card emulated as `saves/card_a.raw`, written in the background.
- Settings menu (hold −): screen mode, resolution, FPS counter, Chaos Emerald gear unlock,
  controllers, screenshots; saved to `settings.ini`.

### Tools
- Windows setup tool (`SonicRidersSetup.bat`): checks the disc image, downloads a SHA-256-pinned
  toolchain, recompiles and builds the Switch and/or PC build, installs to the SD card.
- Optional home-screen forwarder NSP (`tools/nsp/`).
- Diagnostics: CPU profiler, watchdog dumps, hitch and performance logging, scripted input,
  screenshots (see NOTES.md).
- Guard against game data and personal details in git: pre-commit hook and GitHub Actions
  (`tools/check_no_game_data.py`), also used by `tools/make_release.py`.
