# Installing Sonic Riders

[← Back to the README](../README.md)

- [What you need](#what-you-need)
- [Install on Switch](#install-on-switch)
- [Install on PC](#install-on-pc)
- [Install on Android](#install-on-android)
- [Install on Linux / Steam Deck](#install-on-linux--steam-deck)
- [Install on iPhone / iPad](#install-on-iphone--ipad)
- [Updating to a new version](#updating-to-a-new-version)
  - [Switch: connecting the SD card with hekate](#switch-connecting-the-sd-card-with-hekate-no-card-reader-needed)
- [If something goes wrong](#if-something-goes-wrong)
- [What the setup does](#what-the-setup-does)

## What you need

**For every version:**

- **Your own copy of Sonic Riders (USA)** dumped to a disc image: game ID `GXEE8P`, revision 0, a clean
  1:1 `.iso` (1,459,978,240 bytes, SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`, the Redump
  dump "Sonic Riders (USA) (En,Ja,Fr,De,Es,It)"). You can dump your disc with a Wii and CleanRip.
  RVZ/GCZ/CISO images must be converted back to a full ISO first (Dolphin: right-click the game,
  Convert File, format ISO). The European and Japanese versions are not supported.
- **A Windows 10 (version 1803 or newer) or Windows 11 PC**, 64-bit, with an internet connection the
  first time and about 4 GB of free disk space. The PC does the conversion; nothing is downloaded from
  the game. Nothing else to install: the setup downloads its own tools (no Python, Visual Studio,
  devkitPro or 7-Zip needed). **Linux / Steam Deck** builds on the Linux machine itself instead: see
  [For Linux and the Steam Deck](#for-linux-and-the-steam-deck).

**For the Switch:**

- **A Nintendo Switch running custom firmware** (Atmosphère) with the Homebrew Menu, and an SD card
  with about 2 GB free.
- **For 60 fps, an overclock.** At stock clocks races run at a steady 30 fps (settings, Performance:
  *30 fps*, or *60 fps*, which uses 60 where it can). A steady 60 was tested at CPU 1785 / GPU 1152 /
  memory 2133 MHz on an OLED, set with an overclocking homebrew such as sys-clk. Overclock at your own
  risk and within your console's limits.

**For the PC:**

- A graphics card with **OpenGL 3.3** (any from the last ten years). Tested on an NVIDIA RTX 3060.
- A game controller is recommended (Xbox-style, through SDL; up to 4 for multiplayer). Keyboard works too.

**For Android:**

- **A 64-bit Android phone with Android 11 or newer** and Vulkan graphics (almost every phone from the
  last five years). Tested on a Samsung Galaxy S23 Ultra, where races run at 60 fps; other phones are untested.
- About **2 GB free on the phone** (the game plus your disc image) and about **4 GB more on the PC** for the
  Android build tools (a 1.1 GB download the first time, about 3 GB unpacked, kept for updates).
- A **USB cable** between the phone and the PC, and **USB debugging** turned on (step 1 below).

## Install on Switch

1. **Download** `SonicRiders-Switch-Setup-<version>.zip` from the [latest release](../../../releases/latest).
2. **Extract** the whole zip to a folder, for example `C:\SonicRiders`. Don't run it from inside the zip.
   The folder's path must use only English letters and digits: if your Windows user name has accents or
   other letters, don't use your Downloads or Desktop folder.
3. **Put your Switch's SD card in the PC**, or leave it in the Switch and use hekate's USB mode (hekate →
   **Tools** → **USB Tools** → **SD Card**, then plug the Switch into the PC; details in
   [Switch: connecting the SD card with hekate](#switch-connecting-the-sd-card-with-hekate-no-card-reader-needed)).
4. **Double-click `Install Sonic Riders (Switch).exe`.** If Windows shows "Windows protected your PC",
   click *More info* → *Run anyway* (the program is not signed). In its window:
   - **Select disc...**: pick your disc image; the setup checks that it is the right one;
   - **SD card**: your Switch's SD card is found by itself (or press *Other...* and pick its top folder, the
     one containing `atmosphere` and `switch`); keep *Also copy the disc image* ticked the first time;
   - optional: your own icon picture, and the HOME-screen icon (below);
   - press **BUILD AND INSTALL**. The window shows each step and the setup's messages.
   (`Install Sonic Riders (Switch) - command line.bat` does the same in a command window, with questions.)
5. **Wait.** The first run downloads the build tools (about 300 MB, 1.3 GB unpacked, kept for next
   time) and converts the game: 10–30 minutes. It ends with "Done". If something fails, the message
   says why, and the whole log is in `setup.log`.
6. **Eject the SD card safely** and put it back in the Switch.
7. **Start it in full-memory mode:** hold **R** while starting any installed game until the Homebrew
   Menu opens, then choose **Sonic Riders**. Opening the Homebrew Menu from the Album does not give the
   game enough memory.

Hold **−** (Minus) in the game for the settings menu. Your saves and settings stay on the SD card in
`switch/sonicriders/`.

### Optional: a Sonic Riders icon on the HOME screen

The setup can also make a small **HOME-screen icon** (a "forwarder" `.nsp`): it opens the game directly, with
full memory, without holding R over a game. Tick *Make a HOME-screen icon* in the installer window (the
command-line setup asks right after "What to build"):

- **What you need:** your console's **`prod.keys`**, dumped with [Lockpick_RCM](https://github.com/saneki/Lockpick_RCM)
  (the setup finds them in `%USERPROFILE%\.switch\prod.keys` or asks where they are; they are read where they
  are, never copied or uploaded), and on the console **DBI** or **Goldleaf** plus **up-to-date sigpatches**.
- **The picture (optional):** your own picture (`.png`, `.ico`, `.jpg`, `.bmp`; square, **256 x 256** or bigger
  looks best; it is made into the 256 x 256 JPEG the HOME menu shows), or the game's own memory-card icon.
  The same picture is used for the PC and Android icons when you build those too.
- With an SD card selected, the setup copies it to `NSP\SonicRiders-HOME-icon.nsp` on the card (else it stays
  in `build\nsp\`). **Install it once:** DBI → *Browse SD card* → `NSP` → `SonicRiders-HOME-icon.nsp` →
  *Install* (to the SD card); or Goldleaf → *Explore SD card* → the `.nsp` → *Install* → *SD card*. Go back to
  HOME: the icon is there, and the `.nsp` on the card can be deleted.
- The icon starts `switch/sonicriders/sonicriders.nro`: keep the game there. Updating the game = replacing the
  `.nro`; the icon stays. If opening the icon shows an error, update your sigpatches.
- The `.nsp` is packed with your keys: it is yours only, never share it.

Already built the game? Run the setup again (it reuses the downloaded tools) and answer **y** to the HOME-screen
icon question, or by hand: `python tools\nsp\make_forwarder.py --devkitpro <tools>\devkitpro --cc
<tools>\llvm-mingw\bin\x86_64-w64-mingw32-clang.exe --keys <prod.keys> [--icon <picture>]`.

## Install on PC

1. **Download** `SonicRiders-PC-Setup-<version>.zip` from the [latest release](../../../releases/latest).
2. **Extract** the whole zip to a folder **you'll keep**, for example `C:\Games\SonicRiders`. The game,
   your saves and your settings live in that folder.
3. **Double-click `Install Sonic Riders (PC).bat`.** If Windows shows "Windows protected your PC",
   click *More info* → *Run anyway*. Pick your disc image, and say **y** if you want a desktop shortcut.
4. **Wait.** The first run downloads the build tools (a few hundred MB, kept for next time) and builds
   the game: 10–30 minutes. It ends with "Done".
5. **Play:** start **Sonic Riders** with the shortcut in the folder (or on your desktop).

Controls are in [PLAYING.md](PLAYING.md). The game's log is `sonicriders.log` in the install folder
(attach it to bug reports).

## Install on Android

The game is built on your PC and installed on the phone over USB; nothing is downloaded on the phone.

1. **On the phone, turn on USB debugging (once):** Settings → About phone → Software information → tap
   **Build number** 7 times until it says Developer mode is on. Then Settings → **Developer options** →
   **USB debugging**: on. (On other brands Build number is under About phone.)
2. **Download** `SonicRiders-Android-Setup-<version>.zip` from the [latest release](../../../releases/latest)
   and **extract** the whole zip to a folder **you'll keep**, for example `C:\SonicRiders` (only English letters
   and digits in the path).
3. **Connect the phone** with a USB cable and unlock it. If it asks what the USB connection is for, choose
   **File transfer**.
4. **Double-click `Install Sonic Riders (Android).exe`.** If Windows shows "Windows protected your PC", click
   *More info* → *Run anyway*. In its window:
   - **Select disc...**: pick your disc image;
   - read (*Read it*) and tick **I accept the Android SDK License** (the setup uses Google's build tools);
   - keep *Install on the connected phone* ticked, optionally pick your own app icon, and press
     **BUILD AND INSTALL**. When the phone shows **"Allow USB debugging?"**, tap **Allow** (tick *Always allow
     from this computer* to skip it next time).
   (`Install Sonic Riders (Android) - command line.bat` does the same in a command window, with questions.)
5. **Wait.** The first run downloads the Android build tools and builds the game: 20–40 minutes. The setup
   then installs the app and copies your disc image to the phone (about a minute over USB), and checks it.
6. **Play:** start **Sonic Riders** from the phone's app list. You can unplug the cable.

**Controls:** touch controls are on screen. The left half is the control stick (it appears where your thumb
lands); on the right are JUMP (A, hold to charge), BOOST (B), DRIFT (R, hold) and TORNADO (L + R); top right
is START, top left opens the settings menu. Any Bluetooth or USB controller works too (it hides the touch
controls until you touch the screen; hold Back/Select or press the phone's Back for the menu). Two players on
one phone: connect two controllers.

**Your saves** live inside the app's own folder on the phone (`Android/data/net.srnative.sonicriders/files/`).
Uninstalling the app deletes them, so update by installing over it (below), not by uninstalling first.

**If the game crashes or stops with an error,** open it again: it shows "Sonic Riders stopped last time" and
saves a report to the phone's `Download/SonicRiders/` folder. Tap **Share report** (or attach that file from
any file manager) and send it with your bug report.

**Screen recording is not supported:** recording the screen during a race makes the phone's GPU driver reset
the game again and again until it closes (seen on the Galaxy S23 Ultra). A single reset without recording (rare,
on a very warm phone) is survived after the next update: the game pauses for about a third of a second and goes on.

**For Linux and the Steam Deck:**

- A **64-bit Linux** (x86_64) with **Python 3**, **SDL2** and **OpenGL 3.3**. SteamOS has all three.
  No compiler, root rights or package installs needed: the setup downloads its own tools, including the
  zig compiler (about 60 MB) when the system has no clang or gcc. About 4 GB free and internet the first time.
- The Linux version is **new and not yet tested on a Steam Deck** (tested on Ubuntu 24.04). Reports welcome.

## Install on Linux / Steam Deck

1. **Download** `SonicRiders-Linux-Setup-<version>.zip` from the [Linux test pre-release](../../../releases/tag/v0.5.0-linux-test)
   (later releases will include it).
   On the Steam Deck, do steps 1-4 in **Desktop Mode** (Power → Switch to Desktop).
2. **Extract** the whole zip to a folder **you'll keep**, for example `~/Games/SonicRiders` (right-click
   the zip → Extract). The game, your saves and your settings live in that folder. Your disc image can stay
   where it is (on the SD card too); the setup links to it.
3. **Double-click `Install Sonic Riders (Linux).sh`** (choose *Execute* or *Run* if asked). It opens a
   terminal; pick your disc image. Or in a terminal: `sh 'Install Sonic Riders (Linux).sh'`
   (`--iso /path/to/image.iso` skips the file dialog).
4. **Wait** 10–30 minutes the first time. It ends with "Done" and adds **Sonic Riders** to the application
   menu (Games), plus a launcher `Sonic Riders.sh` in the folder.
5. **Steam Deck Game Mode:** in Desktop Mode open Steam → *Games* → *Add a Non-Steam Game to My Library*,
   tick **Sonic Riders** and click *Add*. Back in Game Mode it's in your library under *Non-Steam*, and
   the Deck's controls work as a game controller (hold *View* for the settings menu).

The game's log is `sonicriders.log` in the install folder (attach it to bug reports); the setup's log is
`setup.log`. The downloaded tools are kept in `~/.local/share/SonicRidersSetup/tools` for updates.

## Install on iPhone / iPad

The iPhone/iPad app is built on **a Mac** from your disc image (an app with the game in it can't be
shared, so there is no ready-made download). Tested: races at 60 fps on an iPhone 17 Pro, an iPad Pro 11-inch
(M1) and an iPad (A16), with a short stutter the first time a new effect appears; the iPad 7th
generation (2019) plays with small dips on busy tracks.

**You need:** an **Apple silicon Mac** (M1 or newer) with **Xcode** (App Store, free, about 10 GB; open
it once and add iOS when it asks), **Homebrew** ([brew.sh](https://brew.sh); the setup installs `ninja`
and `shaderc` with it), about 6 GB free, internet the first time, a cable for the device, and an
iPhone/iPad on **iOS / iPadOS 16 or newer**. A free Apple ID is enough.

1. **Download** `SonicRiders-iOS-Setup-<version>.zip`, **double-click** it and keep the folder it makes
   (for example in your home folder). Open **`START HERE (Mac).html`** in it: it shows the first start with
   pictures.
2. **Open the launcher the first time** (once; it's free and not signed by Apple, so macOS asks you to allow it):
   1. Double-click **Sonic Riders Launcher**.
   2. A warning appears ("“Sonic Riders Launcher.app” Not Opened"): click **Done**, the lower grey button.
      **Don't** click the blue *Move to Trash* button above it (it is highlighted, but deletes the launcher).
   3. Open *System Settings* → *Privacy & Security* and scroll down to *Security*: next to "“Sonic Riders
      Launcher.app” was blocked to protect your Mac" click **Open Anyway**. (The button only appears after step 1.)
   4. macOS asks "Open “Sonic Riders Launcher.app”?" (if not, double-click the launcher again): click **Open
      Anyway**, the **middle** button (again not the blue *Move to Trash* on top), then Touch ID or *Use Password…*.
   5. The launcher opens and asks for your **Downloads** and **Desktop** folders: click **Allow** both times.
3. **In the launcher:**
   - **Get ready** checks Xcode, its iOS platform and Metal Toolchain, Homebrew and its two build tools; each
     missing piece has a button that installs or opens it. Click *Check again* after each.
   - **Build:** *Select disc...* (your image), optionally *Choose picture...* for the app icon (PNG, JPG, HEIC,
     WEBP, GIF, BMP or TIFF; the centre square is used and transparent parts turn blue), then **BUILD THE APP**
     (15–30 minutes the first time). The `.ipa` goes to `~/Desktop/Sonic Riders iOS/`.
   - **Install** walks you through Sideloadly step by step: get Sideloadly, connect and *Trust*, install the
     `.ipa` with your Apple ID, **Developer Mode** (*Settings* → *Privacy & Security* → *Developer Mode*, restart),
     trust your Apple ID (*Settings* → *General* → *VPN & Device Management*), and copy your disc image with
     Finder → your device → *Files* tab (wait until the copy has finished before opening the game; if Finder
     says "The required file cannot be found", click another place in the sidebar and the device again).
     With your Apple ID added in Xcode you can pick *Signed* on the Build page and install **from this Mac**
     over the cable instead.
   - **Renew** shows when the 7 days run out, the steps, and adds a Calendar reminder.
4. **Without the launcher** (Terminal): double-click `Install Sonic Riders (iOS).command` (it needs the same
   *Privacy & Security* step) or type `sh ` in Terminal, drag the `.command` file in and press Return. The
   folder path must have no spaces for this route. It asks for your disc image and, optionally, an icon picture.

### Renewing every 7 days

With a free Apple account, the app stops opening about 7 days after it was put on the device. Renewing gives it 7
more days. Your saves and the game file stay on the device.

**With Sideloadly**, one device at a time:

1. Plug the iPhone or iPad into the Mac with a cable and unlock it.
2. Open **Sideloadly** and make sure your device is picked in the box at the top.
3. Drag in **`SonicRiders.ipa`** from your Desktop (folder *Sonic Riders iOS*).
4. Type your Apple ID email if it asks.
5. **Don't change any settings** ("Use automatic bundle ID" stays ticked, as it is by default).
6. Click **Start**, type your Apple ID password if it asks, and wait for "Done".

**Or from the Mac:** in the Sonic Riders launcher go to **Install**, pick the device and click **Install only
(renewing)** (or run `Install Sonic Riders (iOS).command` again). It gets a fresh 7 days first (about a minute).

The same steps are in **`How to renew (every 7 days).txt`** in the *Sonic Riders iOS* folder on your Desktop. If
you ever see **two** Sonic Riders apps on a device, a setting was changed: delete the empty one (the one that asks
for the disc image).

**An older install** (installed before 2026-11-13, ID `net.srnative.sonicriders` or `net.srnative.sonicriders.t…`;
*Settings → About* says "Older app ID"): connect it to the Mac and click **Install** in the launcher (or run the
`.command`). It moves the app to the standard ID by itself: your saves, settings and the disc image are copied to
the Mac, the old app is removed, the new one installed and everything copied back. A copy of the saves stays in
`build/ios/moved/` in the Sonic Riders folder. From then on Sideloadly's default settings renew the same app.

**Ended up with two Sonic Riders apps anyway?** Your save is in the old one. Open the Files app → *On My iPad*
(or *On My iPhone*): there are two *Sonic Riders* folders. Copy `saves/card_a.raw` from the folder that has it
into `saves/` in the other one (and the disc image, if that one has none). Then delete the app you don't want to
keep.

**A free Apple ID allows 3 sideloaded apps per device.** If the install fails with "maximum number of
installed apps", remove another sideloaded app first.

**Playing:** touch controls on screen (the round button at the top left opens the settings menu, START
pauses), or a game controller (Xbox, PlayStation, MFi). Locking the device or opening Control Center
pauses the game. The game's log is `sonicriders.log` in the app's folder (Files app → *On My iPhone* →
*Sonic Riders*); the setup's log is `setup.log` in the install folder.

## Updating to a new version

**The quick way: update from the app itself** (Windows: the PC launcher, the Switch installer and the Android
installer). No GitHub visit, no zip, no settings to pick again.

1. Open the app you installed with (`Sonic Riders` on PC, `Install Sonic Riders (Switch).exe`, or
   `Install Sonic Riders (Android).exe`). When a new version is out it shows **Update available** in gold.
2. **Switch:** connect the SD card first: take it out and put it in the PC, or leave it in the Switch and use
   hekate's USB mode (details below). **Android:** connect the phone as on the first install.
3. Press **See what's new** → **UPDATE NOW**. That's all: it downloads the new version (a few MB), restarts,
   rebuilds and installs it by itself with the same disc image, SD card or phone as last time. Usually under
   a minute. (Switch: if the SD card isn't connected yet, it waits for it and goes on as soon as it appears.)
4. **Switch:** eject the SD card in Windows and go back to the game (below). **PC:** press PLAY.

Your saves, settings, mods and disc image stay. The Switch's HOME-screen icon keeps working (it starts the new
version). This works for updates *from* v0.8.3 on; to get to v0.8.3, use the zip way below once.

**Why there is still a (short) build:** the finished game contains Sonic Riders' own code, converted, so we can
never put it in a download; it has to be made on your PC from your disc. Only the first install does the big
conversion (10-30 minutes). It is kept in the setup's tools folder, so an update only rebuilds the few parts of
the port that changed: a few seconds on a fast PC, usually under a minute. (A release that changes the code
converter itself redoes the conversion: longer, but still automatic and without new downloads.)

### Switch: connecting the SD card with hekate (no card reader needed)

1. Restart the Switch into **hekate** (the way you normally start your custom firmware, or with a "reboot to
   payload" app if hekate is your payload).
2. In hekate: **Tools** → **USB Tools** → **SD Card**.
3. Plug the Switch into the PC with a USB-C cable. The SD card shows up in Windows as a drive. (If Windows
   offers to scan and fix it, choose *Continue without scanning*.)
4. Update (above). When it says "Done", **eject the drive in Windows** (right-click it → *Eject*) and wait
   until Windows says it is safe.
5. In hekate press **Close** (or unplug the cable), then **Home** → **Launch** → your Atmosphère entry, and
   play as usual.

**Without hekate or a card reader:** pick *Don't copy (keep it in build\switch\)* as the SD card, then send
`build\switch\sonicriders.nro` to `switch/sonicriders/` on the Switch with a file-transfer homebrew you
already use (DBI's *Run MTP responder* over USB, or an FTP app over Wi-Fi). Replace only that file.

### The zip way (Linux, iPhone/iPad, or coming from v0.8.2 or older)

1. Download the new zip for your platform from the [latest release](../../../releases/latest) and extract it
   **over your old install folder** (replace the files when asked); a new folder works too.
2. Run its installer (`Install Sonic Riders (Switch).exe`, `Install Sonic Riders (PC).bat`,
   `Install Sonic Riders (Android).exe`, or on Linux `Install Sonic Riders (Linux).sh`) with the same disc
   image (Switch: the SD card connected; Android: the phone connected). It finishes quickly for the same reason
   as above (iPhone/iPad: the Mac still rebuilds everything, for now). Leave *Copy the disc image too* as it is (skipped when the card already has it); the HOME-screen
   icon is not needed again.

Saves: on the Switch they're on the SD card, which an update doesn't touch. On PC and Linux they're in the
install folder: extracting over it keeps them; with a new folder, copy the old folder's `saves` into it. On
Android the update installs over the app and keeps its saves; it must be built **on the same PC**, because
Android only accepts an update signed with the same key (the setup keeps it in
`%LOCALAPPDATA%\SonicRidersSetup\tools\android\`; don't delete it). From another PC you would have to
uninstall first, which deletes the saves.

**If something looks wrong after an update:** check the version in the settings menu (hold **−**, *About* tab). If it is
still the old one, the new `sonicriders.nro` did not reach `switch/sonicriders/`.

To get a notification when a new version comes out, click **Watch** at the top of the GitHub page,
then **Custom** → **Releases**. What changed in each version is in the [CHANGELOG](../CHANGELOG.md).

## If something goes wrong

| Problem | What to do |
|---|---|
| "does not match the known-good dump" | Your image is not the clean USA rev 0 dump (trimmed, compressed or modified). Re-dump or convert to a full ISO. |
| "game ID is ..." | Only the USA release (GXEE8P) works. |
| `CreateProcess failed: The system cannot find the file specified` (paths shown with `?` or odd letters) | Your folder path (often your Windows user name) has letters outside English. Fixed in v0.3.4: extract the zip to a folder like `C:\SonicRiders` and run the installer there. On v0.3.3 or older, also start it with `-ToolsDir C:\SonicRidersTools`. |
| A download fails | Check your internet connection or firewall and run the setup again; finished downloads are kept. |
| `Error opening archive: Can't initialize filter; unable to run program "xz -d -qq"` | Fixed in v0.2.2: download the latest release and run its installer. |
| Switch: black screen or crash at start | Make sure you started from a game with R held (not the Album), and that the `.iso` is in `switch/sonicriders/`. |
| Switch: slow motion / low frame rate | Set the frame rate to *30 fps* or *60 fps* (hold Minus for the settings), or overclock (see [What you need](#what-you-need)). |
| PC: the game doesn't start | Check `sonicriders.log` in the install folder; update your graphics driver (OpenGL 3.3 is needed). |
| PC: no sound | Check Windows' default playback device; the game uses it. |
| Linux: double-clicking the installer opens it in a text editor | Right-click it → *Properties* → *Permissions* → tick *Is executable*, or run `sh 'Install Sonic Riders (Linux).sh'` in a terminal in that folder. |
| Linux: "SDL2 library ... was not found" | Install your distribution's SDL2 package (`libsdl2-2.0-0` on Debian/Ubuntu, `sdl2` on Arch/SteamOS, `SDL2` on Fedora). |
| Android: "0 Android devices are connected" | Unlock the phone, use a data cable (not charge-only), choose File transfer, turn on USB debugging and tap Allow on the phone, then run the installer again. |
| Android: "signatures do not match" (INSTALL_FAILED_UPDATE_INCOMPATIBLE) | The installed app was built on another PC (or the signing key was deleted). Uninstall the app (this deletes its saves) and install again. |
| Android: the game closes on launch or while playing | Open it again and tap **Share report** (the report is also in `Download/SonicRiders/`); attach it to a [bug report](../../../issues/new/choose). |
| Android: the game closes while screen recording | Screen recording is not supported (see [Install on Android](#install-on-android)); reopen the game and play without recording. |
| Anything else | See [Reporting bugs](REPORTING-BUGS.md). |

## What the setup does

It checks your image's hash; downloads a pinned toolchain (Python, ninja, decomp-toolkit, and either
devkitPro's devkitA64/libnx/deko3d for the Switch or llvm-mingw + SDL2 for the PC; every file is
checked against a SHA-256 in `setup/toolchain.json`) into `%LOCALAPPDATA%\SonicRidersSetup\tools`;
extracts `main.dol` and `_Main.rel` from your image; analyses and recompiles them into C; builds; and
(Switch) copies the result to the SD card. No administrator rights are needed.

The build itself happens in `build\` inside that tools folder (Linux: `~/.local/share/SonicRidersSetup/tools/build`),
which is kept for every version: an update copies in only the files that changed and rebuilds only what they
touch, then copies the finished game into the install folder. It holds the converted game code, so like the
game it stays on your computer: never share it. Deleting it is safe; the next run converts the game again.

The SD card ends up with:

```
sdmc:/switch/sonicriders/
    sonicriders.nro             the port
    <your disc image>.iso       read at runtime (music, models, stages...)
    saves/card_a.raw            memory card (created on first save)
    settings.ini                settings menu choices
    sonicriders.log             log of the last run (sonicriders.prev.log: the one before; .prev2-.prev4: older)
```

On Linux the install folder ends up with `Sonic Riders.sh` (the launcher), `build/host-linux/sonicriders`,
`baserom/` (a link to your disc image), `saves/` and `sonicriders.log`, and the menu entry is
`~/.local/share/applications/sonicriders.desktop`.

The PC install folder ends up with `Sonic Riders.lnk` (the shortcut), `build\host-mingw\sonicriders.exe`
(the game), `baserom\` (a link to your disc image), `saves\` (memory card and settings) and
`sonicriders.log`.
