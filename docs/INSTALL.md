# Installing Sonic Riders

[← Back to the README](../README.md)

- [What you need](#what-you-need)
- [Install on Switch](#install-on-switch)
- [Install on PC](#install-on-pc)
- [Updating to a new version](#updating-to-a-new-version)
- [If something goes wrong](#if-something-goes-wrong)
- [What the setup does](#what-the-setup-does)

## What you need

**For both versions:**

- **Your own copy of Sonic Riders (USA)** dumped to a disc image: game ID `GXEE8P`, revision 0, a clean
  1:1 `.iso` (1,459,978,240 bytes, SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`, the Redump
  dump "Sonic Riders (USA) (En,Ja,Fr,De,Es,It)"). You can dump your disc with a Wii and CleanRip.
  RVZ/GCZ/CISO images must be converted back to a full ISO first (Dolphin: right-click the game,
  Convert File, format ISO). The European and Japanese versions are not supported.
- **A Windows 10 (version 1803 or newer) or Windows 11 PC**, 64-bit, with an internet connection the
  first time and about 4 GB of free disk space. The PC does the conversion; nothing is downloaded from
  the game. Nothing else to install: the setup downloads its own tools (no Python, Visual Studio,
  devkitPro or 7-Zip needed).

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

## Install on Switch

1. **Download** `SonicRiders-Switch-Setup-<version>.zip` from the [latest release](../../../releases/latest).
2. **Extract** the whole zip to a folder, for example `C:\SonicRiders`. Don't run it from inside the zip.
   The folder's path must use only English letters and digits: if your Windows user name has accents or
   other letters, don't use your Downloads or Desktop folder.
3. **Put your Switch's SD card in the PC** (or connect the Switch over USB with a tool that shows the
   SD card as a drive).
4. **Double-click `Install Sonic Riders (Switch).bat`.** If Windows shows "Windows protected your PC",
   click *More info* → *Run anyway* (the script is not signed). Then answer its questions:
   - pick your disc image in the file window; the setup checks that it is the right one;
   - say **y** to copy to the SD card and pick the SD card's top folder (the one containing
     `atmosphere` and `switch`);
   - say **y** to copy your disc image to the SD card (needed once; skipped automatically later).
5. **Wait.** The first run downloads the build tools (about 300 MB, 1.3 GB unpacked, kept for next
   time) and converts the game: 10–30 minutes. It ends with "Done". If something fails, the message
   says why, and the whole log is in `setup.log`.
6. **Eject the SD card safely** and put it back in the Switch.
7. **Start it in full-memory mode:** hold **R** while starting any installed game until the Homebrew
   Menu opens, then choose **Sonic Riders**. Opening the Homebrew Menu from the Album does not give the
   game enough memory.

Hold **−** (Minus) in the game for the settings menu. Your saves and settings stay on the SD card in
`switch/sonicriders/`.

**Optional home-screen icon:** `tools/nsp/` builds a small forwarder NSP that starts the `.nro` from
the home menu. It needs your own console keys and is for users who already know how to install NSPs;
see `tools/nsp/build_nsp.ps1`.

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

## Updating to a new version

1. Download the new zip for your platform from the [latest release](../../../releases/latest) and
   extract it to a **new** folder.
2. Run its installer (`Install Sonic Riders (Switch).bat` or `Install Sonic Riders (PC).bat`) with the
   same disc image (and, for the Switch, the same SD card).
3. It reuses the build tools it downloaded last time, so it's quicker than the first install (a few
   minutes).

Your saves and settings are kept: on the Switch they're on the SD card, which the update doesn't touch.
On PC they're in the old install folder: copy its `saves` folder into the new one, then delete the old
folder.

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
| Anything else | See [Reporting bugs](REPORTING-BUGS.md). |

## What the setup does

It checks your image's hash; downloads a pinned toolchain (Python, ninja, decomp-toolkit, and either
devkitPro's devkitA64/libnx/deko3d for the Switch or llvm-mingw + SDL2 for the PC; every file is
checked against a SHA-256 in `setup/toolchain.json`) into `%LOCALAPPDATA%\SonicRidersSetup\tools`;
extracts `main.dol` and `_Main.rel` from your image; analyses and recompiles them into C; builds; and
(Switch) copies the result to the SD card. No administrator rights are needed.

The SD card ends up with:

```
sdmc:/switch/sonicriders/
    sonicriders.nro             the port
    <your disc image>.iso       read at runtime (music, models, stages...)
    saves/card_a.raw            memory card (created on first save)
    settings.ini                settings menu choices
    sonicriders.log             log of the last run (sonicriders.prev.log: the one before)
```

The PC install folder ends up with `Sonic Riders.lnk` (the shortcut), `build\host-mingw\sonicriders.exe`
(the game), `baserom\` (a link to your disc image), `saves\` (memory card and settings) and
`sonicriders.log`.
