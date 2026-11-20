# Sonic Riders Recomp — Nintendo Switch

## Stable Release

Checked for **v0.8.7** on November 18, 2026.

Follow these steps to install Sonic Riders on your console. No coding is required.

[Illustrated HTML booklet](install-switch.html) · [Choose your console](index.md) · [Nintendo Switch 2 — Alpha](install-switch2.md)

Tick the checkpoints as you go. Pictures have text descriptions.

## Installation steps

1. [Before you start](#ready)
2. [Prepare your game image](#disc)
3. [Download the correct setup package](#download)
4. [Extract the ZIP file](#extract)
5. [Connect the SD card](#card)
6. [Open the installer](#open)
7. [Select your game image](#choose-disc)
8. [Select the SD card](#choose-card)
9. [Configure installation options](#options)
10. [Build and install](#build)
11. [Open the output folder](#output)
12. [Copy the files manually](#manual)
13. [Verify the SD card folder structure](#folders)
14. [Safely eject the SD card](#eject)
15. [Launch Sonic Riders](#launch)
16. [Verify the installed version](#play)
17. [Update without losing your progress](#update)

[Help](#help) · [Report a bug](#report)

<a id="ready"></a>

## 01. Before you start

![Illustration: Nintendo Switch, Windows computer, prepared SD card, card reader, internet and your own game disc.](images/installation/switch-ready.svg)

*Illustration, not a screenshot.*

1. Check the console’s name. This booklet is for **Nintendo Switch**. Switch, Switch Lite, and Switch OLED use this stable package. If the console says Switch 2, use the other booklet.
2. Check that homebrew already works: **Atmosphère custom firmware** and **Homebrew Menu**, already working. Custom firmware lets your console run community-made apps.
3. If homebrew is new to you, start with [Nintendo Homebrew’s Switch setup guide](https://switch.hacks.guide/). Finish that setup before coming back here. The Sonic Riders installer does not set up homebrew.
4. Use a **64-bit Windows 10 (1803 or newer) or Windows 11 computer**. Connect it to the internet. Windows 11 is recommended. A Mac, phone, or Chromebook cannot run this Windows installer.
5. Make at least **4 GB of free space** on the computer. Extra room is helpful if your disc needs converting.
6. Have the **microSD card already used by your working Switch homebrew setup**. Leave at least **2 GB free**, plus room for any texture packs. Have an SD card reader for your computer.
7. Have your own **Sonic Riders for GameCube, USA, revision 0** disc image. The next page explains this file.
8. Keep the computer awake and plugged in while it builds. Allow **10–40 minutes** for a first install. Download speed and computer speed can change this time.

> A homebrew menu is a list of community-made apps. If you cannot open it yet, finish the linked homebrew setup first. Do not format a card that already has your files on it.

- [ ] **Checkpoint:** You have a working homebrew menu, a Windows computer, free space, and your own game image.

<a id="disc"></a>

## 02. Prepare your game image

![Illustration: your own USA GameCube disc becomes a full ISO file on your computer; the installer checks it.](images/installation/switch-disc.svg)

*Illustration, not a screenshot.*

1. Find your own **USA GameCube Sonic Riders** disc. This is the original Sonic Riders, not Sonic Riders: Zero Gravity.
2. A **disc image** is one file holding a copy of a disc. A full image usually ends in `.iso`. Copying a disc to this file is called **dumping**.
3. If you need to dump it, open the [illustrated Wii dumping walkthrough](https://wii.hacks.guide/dump-games.html#dumping-a-disc-to-sd-usb-cleanrip). It uses **CleanRip**, a disc-copying homebrew app. Follow its **GameCube** section. You need a Wii that can read GameCube discs and a working Homebrew Channel. The Wii mini, ordinary Wii Family Edition, and Wii U’s Wii mode cannot read these discs.
4. Let CleanRip finish. Copy the resulting full `.iso` from its storage device to a folder on your computer. Keep the original file safe. Follow the dumping guide’s file-joining instructions if your dump is split into parts.
5. Already have a dump? You can select a clean `.iso` or `.gcm`. A clean Dolphin `.rvz` or `.wia` also works. The installer converts it to `.iso` for you. Your original file stays unchanged.
6. Use a clean, full copy. European, Japanese, modified, trimmed, incomplete, GCZ, and CISO images are not accepted. An RVZ made with **Remove Junk Data** is not a full copy. Renaming a file cannot turn it into the right game.

> The installer contains no game. Use your own disc and follow the rules where you live. We do not provide game downloads.

**For helpers:** accepted disc ID `GXEE8P`, revision `0`; full ISO size `1,459,978,240 bytes`; SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`. The installer checks this fingerprint automatically. A filename alone does not prove that a dump is supported.

- [ ] **Checkpoint:** Your own full game image is on the computer. You know where you saved it.

<a id="download"></a>

## 03. Download the correct setup package

![Illustration of the GitHub download choices: select SonicRiders-Switch-Setup-v0.8.7.zip under Assets, not Source code.](images/installation/switch-release.svg)

*Illustration, not a screenshot.*

1. Open the [Nintendo Switch release page](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7). GitHub is the website that holds this project’s downloads. You can read the page and download without an account.
2. Check the heading. This page should say **v0.8.7**. This is the stable release for this booklet.
3. Scroll down to **Assets**. Assets means the downloadable files. If the list is folded up, click **Assets** to open it.
4. Click **`SonicRiders-Switch-Setup-v0.8.7.zip`**. You can also use the big download button below.
5. Wait for the download to finish. Do not choose **Source code (zip)** or **Source code (tar.gz)**. Those are for developers and do not contain the ready-to-use Windows installer.

**[Download SonicRiders-Switch-Setup-v0.8.7.zip](https://github.com/teclilla18/sonic-riders-recomp/releases/download/v0.8.7/SonicRiders-Switch-Setup-v0.8.7.zip)**

> A PC, Android, or Linux setup ZIP will not install the Switch version. The separate Switch 2 ZIP is for a different console.

- [ ] **Checkpoint:** The downloaded filename is exactly `SonicRiders-Switch-Setup-v0.8.7.zip`.

<a id="extract"></a>

## 04. Extract the ZIP file

![Illustration: right-click the downloaded ZIP, choose Extract All, then open the normal extracted folder.](images/installation/switch-extract.svg)

*Illustration, not a screenshot.*

1. Open **File Explorer**. It is the yellow folder app on the Windows taskbar, at the bottom of the screen.
2. Click **Downloads** in the left column. Find `SonicRiders-Switch-Setup-v0.8.7.zip`. If you chose a different download location, open that folder instead.
3. Click the ZIP with the **right mouse button**. Choose **Extract All…**. A ZIP stores several files in one file. Extracting restores those files to a normal folder.
4. In the extraction window, choose a short place you can find again, such as `C:\SonicRiders`. Use English letters and numbers in the path. Spaces work, but accented characters in the path can stop the tools.
5. Click **Extract**. Wait until it finishes. Keep all the extracted files together.
6. Open the extracted folder. Open `SonicRiders-Switch-v0.8.7` if there is one more folder inside. Stop when you can see `Install Sonic Riders (Switch).exe`, `setup`, and `tools` together.

> Do not double-click the installer while browsing inside the ZIP. Copying only the EXE is not enough. [Windows extraction help](https://support.microsoft.com/en-us/windows/experience/storage-filemanagement/zip-and-unzip-files).

- [ ] **Checkpoint:** You see `Install Sonic Riders (Switch).exe` beside the `setup` and `tools` folders. The address at the top does not end in `.zip`.

<a id="card"></a>

## 05. Connect the SD card

![Illustration: fully power off, put the prepared SD card in a reader, then open the top level in This PC.](images/installation/switch-card.svg)

*Illustration, not a screenshot.*

1. Quit any running game. Power the console fully off using its power menu. Sleep mode is not power off. Follow your homebrew setup’s shutdown instructions.
2. Remove the prepared SD card. Put it in your computer’s card reader. If your computer has no matching slot, use a suitable USB reader.
3. Open File Explorer. Click **This PC** in the left column. Look for the card under **Devices and drives**. A drive is a storage device shown by Windows.
4. Open the card. Its **root** means its very top folder. You should be looking at the existing `switch` folder, alongside your other homebrew files. Do not open `switch` yet.
5. If a working homebrew card has no `switch` folder yet, make one at this top level: right-click an empty space, choose **New → Folder**, and type exactly `switch`. Leave the other folders alone.
6. If Windows asks you to format the card, click **Cancel**. Formatting erases files. Check that the reader supports the card and that it is the right card.

> No reader? A working hekate setup can expose the card over USB: **Tools → USB Tools → SD Card**. Connect a USB-C cable to the PC. Follow [hekate’s own USB instructions](https://github.com/CTCaer/hekate). This is an optional alternative to removing the card.

- [ ] **Checkpoint:** Windows can show the existing homebrew files on your card. You have not formatted or erased it.

<a id="open"></a>

## 06. Open the installer

![Actual published Nintendo Switch installer. Numbered markers identify Select disc, SD card choices, optional settings and Build and Install.](images/installation/switch-installer.png)

*Actual screenshot with annotations.*

1. Go back to the normal folder you extracted on your computer.
2. Double-click **`Install Sonic Riders (Switch).exe`**. Double-click means press the left mouse button twice quickly.
3. Windows may hide the `.exe` ending. In that case, look for the same name with **Application** in the Type column. Do not open the command-line BAT file for this illustrated guide.
4. Check the installer window. It should identify **Switch installer**.
5. If Windows blocks the program, first check that the ZIP came from the exact project release linked above. This installer is unsigned. If you cannot verify the download, stop and ask someone you trust to help. Do not turn off Windows security.

> The colorful picture across the top is part of the real installer. Your window may be a different size. You can make it larger by clicking the square at the top right.

- [ ] **Checkpoint:** The Nintendo Switch installer is open. You are still on the computer.

<a id="choose-disc"></a>

## 07. Select your game image

![Actual installer detail: Select disc button and the place where the selected game filename appears.](images/installation/switch-select-disc.png)

*Actual screenshot with annotations.*

1. Click the pink-marked **Select disc…** button shown in the picture.
2. In the file window, open the folder where you saved your own game dump. Single-click the `.iso`, `.gcm`, `.rvz`, or `.wia` file.
3. Click **Open**. This selects the file for the installer. You do not need to double-click or mount an ISO in Windows.
4. Look below **Select disc…**. Your selected file’s name should now appear. The installer performs the full game checks when you start the build.

- [ ] **Checkpoint:** The installer shows the game image you chose. It no longer says “No disc image selected”.

<a id="choose-card"></a>

## 08. Select the SD card

![Actual installer detail: SD dropdown, Refresh and Other buttons. The copy-disc checkbox appears after selecting a destination.](images/installation/switch-select-card.png)

*Actual screenshot with annotations.*

1. Find **2. SD CARD** in the installer.
2. Click **Refresh** if you inserted the card after opening the installer.
3. Click the small downward arrow on the SD card box. Choose your card if it is listed.
4. If your card is missing from that list, click **Other…**. In the folder picker, choose **This PC**, then your SD card’s top folder. Click the folder picker’s confirmation button. Do not choose the folder holding the installer.
5. Check that the card you picked is the same card you opened in the previous page. For a first install, keep **Copy the disc image too** ticked. This puts the game’s data beside the program.
6. If you want to copy files later, choose **Keep build on this PC** instead. This makes files on the computer only. Use the manual-copy page below afterward.

> Automatic detection is only a suggestion. Always check the card yourself. A missing card in the list does not mean you need to format it.

- [ ] **Checkpoint:** For the easy route, the installer shows your SD destination and “Copy the disc image too” is ticked.

<a id="options"></a>

## 09. Configure installation options

![Actual installer detail: optional custom picture, HOME-screen icon and texture-pack buttons.](images/installation/switch-options.png)

*Actual screenshot with annotations.*

1. Leave **Use my own picture** unticked for a first install. The installer will use its default game icon. If you choose your own picture later, tick it and use **Pick picture…**. A square picture at least 256 × 256 works best.
2. Leave **Make a HOME-screen icon** unticked for this first install. It is optional. The game will launch from Homebrew Menu without it.
3. If you tick the HOME-screen option later, **Pick prod.keys…** needs your own console’s keys. The option also needs separate console installation steps. Follow the [advanced HOME icon instructions](INSTALL.md#optional-a-sonic-riders-icon-on-the-home-screen) with an experienced helper. Never download or share console keys.
4. Leave **4. TEXTURE PACKS (OPTIONAL)** alone for the first test. Texture packs replace how the game looks. They are not needed to play.
5. Later, **Add texture pack…** accepts your downloaded ZIP, 7z, or RAR pack. **Add a pack folder…** accepts an unpacked folder. Read the [texture pack instructions](https://github.com/teclilla18/sonic-riders-recomp#hd-texture-packs-pc-and-switch) first. Large packs need extra SD space and can pause the game when a new texture loads.

> The first installation needs your game image, the correct SD card, and Copy the disc image too. Custom pictures, HOME icons and texture packs are optional.

- [ ] **Checkpoint:** The required options are selected. Optional extras are not needed for the first installation.

<a id="build"></a>

## 10. Build and install

![Illustration: checks, tools, build and copy lead to Done; a failed build leads to setup.log.](images/installation/switch-build.svg)

*Illustration, not a screenshot.*

1. Click **BUILD AND INSTALL** at the bottom right. If it is dimmed, read the message at the bottom. It tells you which required choice is missing.
2. Leave the installer open. It checks your image, downloads its tools, and builds the program from your own disc. **Build** means make the program that your console can run.
3. Watch the progress messages. Some steps take several minutes. A changing log is a sign that it is working. Keep the internet connected and the SD card plugged in.
4. Do not press **Stop** unless you want to cancel. Do not close the window, unplug the card, or put the computer to sleep while files are being written.
5. Wait for **Done** or **INSTALLATION COMPLETE**. If you see **INSTALL FAILED**, go to the help section. Having a folder named `build` does not by itself mean the build succeeded.

> You do not need to install Python, Visual Studio, devkitPro or another development kit yourself. The installer fetches its own checked tools.

- [ ] **Checkpoint:** The installer says the installation completed. There is no failure message.

<a id="output"></a>

## 11. Open the output folder

![Actual completed installer screenshot: Done, local output path and Open Output Folder highlighted; build log hidden for privacy.](images/installation/switch-completion.png)

*Actual screenshot with annotations.*

1. After success, click **Open Output Folder** in the bottom action bar. The button stays there when you resize the window.
2. File Explorer opens a folder **on your computer**: `build/switch/`. It opens this local folder even if you already disconnected the SD card.
3. Look for `sonicriders.nro`. An `.nro` is a homebrew app that your console can launch.
4. If the installer already copied to the correct SD card and reported success, continue to **Verify the SD card folder structure**. Opening this folder does not copy anything by itself.

> The local output folder is not your SD card. If you chose to keep the build on the PC, you still need the next page.

- [ ] **Checkpoint:** The local folder contains sonicriders.nro.

<a id="manual"></a>

## 12. Copy the files manually

![Illustration: copy the console app and full game image into /switch/sonicriders/, not the whole Windows installer folder.](images/installation/switch-copy.svg)

*Illustration, not a screenshot.*

1. Use this page only if the installer did not copy to your SD card. Keep the installer’s successful output folder open.
2. Open a second File Explorer window. Open **This PC**, then your SD card.
3. Open the card’s `switch` folder. If `sonicriders` is missing inside it, right-click an empty space, choose **New → Folder**, and name it exactly `sonicriders`.
4. In the local output folder, single-click `sonicriders.nro`. Press **Ctrl+C** to copy it.
5. Open `/switch/sonicriders/` on the card. Press **Ctrl+V** to paste. Wait for the copy to finish.
6. Copy your own **full ISO or GCM** into that same folder. An RVZ or WIA cannot be used directly on the console. If the installer converted one, use the generated `.iso` from the extracted installer’s `baserom` folder.
7. Keep just one supported game image in this game folder. Its name may differ from the example. It must really end in `.iso` or `.gcm`, not `.rvz`, `.zip`, or `.iso.iso`.

> Do not copy the Windows EXE, the ZIP, the tools folder, or the whole build folder to the SD card. If adding texture packs later, follow the separate mod instructions.

- [ ] **Checkpoint:** The app and your full disc image sit together inside `/switch/sonicriders/`.

<a id="folders"></a>

## 13. Verify the SD card folder structure

![Illustrated correct SD tree for Nintendo Switch, plus a crossed-out extra nested switch folder.](images/installation/switch-folders.svg)

*Illustration, not a screenshot.*

1. On the card, open `switch`, then `sonicriders`. Compare it with the picture below.
2. Check the exact app name: **`sonicriders.nro`**. Do not rename another platform’s app to this name.
3. Check that your own full `.iso` or `.gcm` is in the **same folder** as the NRO. The example uses `SonicRiders.iso`; the installer keeps your ISO’s original name.
4. If Windows hides file endings, use **View → Show → File name extensions** on Windows 11. On Windows 10, open **View** and tick **File name extensions**. Now check that the app does not end in `.nro.nro`.
5. Leave your other homebrew folders alone. This picture shows only the part that matters to Sonic Riders.

### The files needed to start

```text
SD card (top folder)
└── switch/
    └── sonicriders/
        ├── sonicriders.nro
        └── SonicRiders.iso   ← your full image; its name may differ
```

### After playing or adding packs

```text
/switch/sonicriders/
├── saves/             ← your game progress; keep this
├── settings.ini       ← your choices; keep this
├── mods/              ← texture packs/mods, if added; keep this
└── cache files        ← may appear after playing; keep these
```

- [ ] **Checkpoint:** The path is exactly `/switch/sonicriders/sonicriders.nro`, with the full image beside it.

<a id="eject"></a>

## 14. Safely eject the SD card

![Illustration: completed file copy, Eject, Safe to remove, then reinsert the card in the powered-off console.](images/installation/switch-eject.svg)

*Illustration, not a screenshot.*

1. Wait for all copy windows to finish. Close any files or Explorer windows using the card.
2. If the installer has your SD card selected, click **Eject SD card**. Wait for **Safe to remove the SD card now.**
3. Or use Windows: click the small **up arrow** near the clock at the bottom right. Right-click **Safely Remove Hardware and Eject Media**. Choose **Eject** for your card or reader.
4. Wait for Windows to say the hardware can be removed. If it says the card is in use, close the program using it and try again. Do not pull it out during a copy.
5. Take the card out of the reader. With the console fully powered off, put the card back in it. Start your usual working homebrew setup.

> [Microsoft’s illustrated safe-removal help](https://support.microsoft.com/en-us/windows/hardware/safely-remove-hardware-in-windows). If you used hekate’s USB storage instead, eject in Windows before ending USB storage in hekate and booting your usual custom firmware.

- [ ] **Checkpoint:** The card is back in the console after a safe eject.

<a id="launch"></a>

## 15. Launch Sonic Riders

![Illustration, not console capture: hold R over an installed game, choose Sonic Riders in Homebrew Menu, then reach the title screen.](images/installation/switch-launch.svg)

*Illustration, not a screenshot.*

1. Start the console in your already-configured **Atmosphère** environment.
2. On HOME, highlight an installed game you can launch. Hold the controller’s **R shoulder button** while opening the game. Keep holding R until Homebrew Menu opens. If it asks for a user, keep R held while choosing one.
3. Use this game-launch route, called **full-memory mode** or **title override**. Opening Homebrew Menu through **Album** gives less memory and is not the route for this game.
4. In Homebrew Menu, use the stick or D-pad to highlight **Sonic Riders**. Press **A**.
5. Wait for the game to start. You should reach the intro or title screen. Follow the game’s on-screen prompts. It may ask to create a memory-card save on the first run.
6. When you reach the main menu, you have completed the install. Try a short race before adding optional packs or changing many settings.

> The R button is the small right shoulder button, not ZR and not the right stick. A separate HOME-screen icon is optional and is outside this first-install route.

- [ ] **Checkpoint:** Sonic Riders reaches its title screen and menu. A successful PC build alone is not this checkpoint.

<a id="play"></a>

## 16. Verify the installed version

![Illustration: hold Minus, open About to check the version, and use Quit Game for a normal exit.](images/installation/switch-play.svg)

*Illustration, not a screenshot.*

1. In the game, hold **Minus (−)** for about half a second. This opens the port’s settings menu.
2. Open **About**. Check the version/build information against **v0.8.7**. If an old version is still shown, follow **Update without losing your progress** below.
3. For a first test, keep the normal settings. Stock-clock consoles can run races at 30 FPS. This booklet does not require overclocking.
4. To quit normally, open the Minus menu, choose **Quit Game**, and confirm. Wait to return to Homebrew Menu. Do not remove the SD card while the game is saving.

- [ ] **Checkpoint:** The version/build is the one you installed. You can open settings and quit normally.

<a id="update"></a>

## 17. Update without losing your progress

![Illustration: back up the game folder, replace only the program files, and keep saves, settings, texture packs and caches.](images/installation/switch-update.svg)

*Illustration, not a screenshot.*

1. Quit Sonic Riders normally before you update. Connect the SD card to your computer using the same safe steps as before.
2. **Back up first.** Copy the whole `/switch/sonicriders/` folder from the SD card to a safe folder on your computer. A backup is a spare copy. Keep it until you know the update works.
3. Open the [Nintendo Switch release page](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7). For a future update, check for a newer release for **this same platform**. Download its matching setup ZIP.
4. Extract the new ZIP to a **new normal folder** on the PC. Open its installer. Select your same own game image and the same SD card root.
5. Build and install. Keep the existing full game image if it is already verified on the card; **Copy the disc image too** can be unticked for that case.
6. For a manual update, copy **only** the new `sonicriders.nro` into the existing game folder and replace that file.
7. Keep `saves/`, `settings.ini`, `mods/`, `env.txt` if present, and cache files. Your texture packs live under `mods/`. Do not delete the old game folder or replace it with an empty folder.
8. Eject safely. Launch again. Open **Minus → About** to check the version/build. Also check that your save and chosen settings are still there.

> The stable installer also has **See what’s new → UPDATE NOW** on supported installed versions. Connect the same SD card first. The manual route here is useful if that updater fails. Issue #31 reports a Switch Lite still showing 0.1.0 after an update; its cause is unresolved. Do not erase your save to try to fix a version number.

- [ ] **Checkpoint:** The new version/build appears, and your progress, settings and packs are still present.

<a id="help"></a>

## Picture-by-picture help

### My download does not contain the right installer

Check the full ZIP name: `SonicRiders-Switch-Setup-v0.8.7.zip`. Open the release page and choose that asset. **Source code (zip)** is not the setup package. Keep all extracted files together.

[Picture and steps](#download).

### The installer cannot find setup files

Close it. Right-click the original ZIP and choose **Extract All…**. Open the normal extracted folder. The EXE, `setup`, and `tools` must be together. Do not run a lone EXE copied out of the ZIP.

[Picture and steps](#extract).

### It says the game image is wrong

Read the exact message. Only the clean USA GameCube disc, revision 0, is supported. Select the full correct dump. For a checksum or incomplete-copy error, dump your own disc again using CleanRip’s checks. A different filename cannot fix different contents. RVZ/WIA must preserve the original disc data. See the full-image fingerprint on the disc page.

[Picture and steps](#disc).

### The app or disc file is missing

Wait for a successful build first. Open the output folder and copy `sonicriders.nro` to `/switch/sonicriders/`. Put your full ISO/GCM beside it. Show file name extensions to catch `.nro.nro` or `.iso.iso`. RVZ stays on the PC; copy the converted ISO from `baserom` instead.

[Picture and steps](#folders).

### My folders look different

The card needs `/switch/sonicriders/sonicriders.nro`. An extra `switch` inside `switch` is wrong. So is copying the Windows setup folder to the card. Open the correct destination and copy only the files shown in the finished-tree picture. Preserve the other apps already on the card.

[Picture and steps](#folders).

### Sonic Riders is not in Homebrew Menu

Check that you are using the correct platform’s working Homebrew Menu. Check for `sonicriders.nro` directly in `/switch/sonicriders/`. Eject safely after correcting the files, then reopen the menu. A ZIP or Windows EXE will not appear as a console app.

[Picture and steps](#launch).

### The SD card is not found

Check that File Explorer can open the card. In the installer, press **Refresh**, then try **Other…** and pick its top folder. If Windows asks to format it, cancel. Check the reader and your existing homebrew setup before continuing.

[Picture and steps](#card).

### The installer says INSTALL FAILED

Keep the window open and read the last error. Find `setup.log` beside the installer EXE. If it reports insufficient space, free the stated space. If it reports a download failure, restore your connection and retry. If it reports unsupported path characters, extract to the short English-letter PC folder shown earlier. If a hash check fails, do not bypass it. Save the log and report the error if retrying does not fix it.

[Picture and steps](#build).

### I get a black screen or a crash

First check that you launched Homebrew Menu by holding R while opening an installed game. Album mode has too little memory. Next check the full ISO/GCM and exact folder. These checks fix setup mistakes; they do not establish the cause of every crash. If it still fails, preserve the logs and report it.

[Picture and steps](#launch).

### Known problems that are still open

**Unresolved:** rare hangs on **Now Loading** are tracked in [issue #6](https://github.com/teclilla18/sonic-riders-recomp/issues/6). Texture packs can briefly freeze the original Switch when new textures load. Test a fresh install before adding packs. There is no verified fix for every loading hang. An old version still appearing after an update is reported in [issue #31](https://github.com/teclilla18/sonic-riders-recomp/issues/31); its cause is still being investigated.

[Picture and steps](#play).

### About still shows the old version

Compare the new local NRO with the file in `/switch/sonicriders/`. Make sure you copied to the card you actually use. Follow the manual update route, then launch that app again and check **Minus → About**. Keep your backup and save. If the version remains old, report both the downloaded ZIP name and the displayed version. Do not assume this identifies the cause of issue #31.

[Picture and steps](#update).

<a id="report"></a>

## Report a problem

![Illustration: a useful report includes steps and cleaned log copies.](images/installation/switch-report.svg)

1. For an installer problem, open the normal extracted setup folder. Find **`setup.log`** beside the installer EXE. Open it in Notepad. Save a copy before retrying.
2. For a console problem, quit normally if possible. Before launching again, copy **`sonicriders.log`** and **`sonicriders.prev.log`**, if present, from `/switch/sonicriders/` to the computer.
3. Read the copies before sharing. Replace personal paths, names, IP addresses or other private details with `[removed]`. Do not remove the error messages.
4. Open the [bug report form](https://github.com/teclilla18/sonic-riders-recomp/issues/new?template=bug_report.yml). A free GitHub account is needed to post a report. Reading guides and downloading do not need one.
5. Write **Nintendo Switch**, the ZIP filename, version/build shown in About, console model, firmware, homebrew loader/version, and exactly what you clicked or played. Say handheld or docked, stage, character, gear, player count, settings and any known clock settings. If you do not know a number, say “unknown”.
6. Add the error text and the cleaned log copies. Say what you expected, what happened, and whether you can make it happen again. A cropped photo of the error can help; hide private details.
7. Never attach your ISO/RVZ, NRO, saves, console keys, passwords, generated code or game assets. Those are not needed for a useful public report.

### Bug report template

```text
Platform: Nintendo Switch
Setup ZIP: SonicRiders-Switch-Setup-v0.8.7.zip
Version/build in About: …
Firmware and homebrew loader/version: …
Handheld or docked; known clocks: …
What I did, one step at a time: …
What I expected: …
What happened / exact error: …
Does it happen again? …
Cleaned log copies attached: …
```

## Sources and verification

The current published ZIPs were downloaded and hash-checked. Their installers were run on Windows into disposable SD folders; disc copies and updates were checked. The actual installer captures come from these published executables. Console steps use existing project hardware reports and the linked homebrew instructions. No console was reinstalled or play-tested for this booklet. Drawn console screens are clearly marked illustrations.

[Exact package hashes, sources and picture credits](guide-verification.md). The visual reference is the project’s existing Claude-made easy-read guides. Technical documentation remains in [INSTALL.md](INSTALL.md).
