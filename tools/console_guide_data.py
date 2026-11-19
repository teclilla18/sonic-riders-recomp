"""Player-facing booklet content. Render HTML and Markdown with make_console_guides.py."""

REPO = 'https://github.com/teclilla18/sonic-riders-recomp'
WII = 'https://wii.hacks.guide/dump-games.html#dumping-a-disc-to-sd-usb-cleanrip'
ZIP_HELP = 'https://support.microsoft.com/en-us/windows/experience/storage-filemanagement/zip-and-unzip-files'
EJECT_HELP = 'https://support.microsoft.com/en-us/windows/hardware/safely-remove-hardware-in-windows'


def chapter(key, title, actions, picture, alt, checkpoint, *, note='', extra='', tone='new'):
    return dict(key=key, title=title, actions=actions, picture=picture, alt=alt,
                checkpoint=checkpoint, note=note, extra=extra, tone=tone)


def guide(native=False):
    platform = 'Nintendo Switch 2' if native else 'Nintendo Switch'
    mode = 'switch2' if native else 'switch'
    edition = 'Experimental Alpha Release' if native else 'Stable Release'
    version = 'Alpha 2 · v0.8.7-switch2-alpha.2' if native else 'v0.8.7'
    tag = 'v0.8.7-switch2-alpha.2' if native else 'v0.8.7'
    archive = 'SonicRiders-Switch2-Setup-Alpha2.zip' if native else 'SonicRiders-Switch-Setup-v0.8.7.zip'
    extracted = 'SonicRiders-Switch2-Alpha2' if native else 'SonicRiders-Switch-v0.8.7'
    exe = 'Install Sonic Riders (Switch2).exe' if native else 'Install Sonic Riders (Switch).exe'
    app = 'sonicriders-switch2' if native else 'sonicriders'
    output = 'build/switch2/sd/switch/sonicriders-switch2' if native else 'build/switch'
    release = REPO + '/releases/tag/' + tag
    download = REPO + '/releases/download/' + tag + '/' + archive
    local = 'C:\\SonicRidersAlpha' if native else 'C:\\SonicRiders'
    sdpath = '/switch/' + app + '/'
    other = 'install-switch.html' if native else 'install-switch2.html'
    other_md = other.replace('.html', '.md')
    other_name = 'Nintendo Switch — Stable' if native else 'Nintendo Switch 2 — Alpha'
    homebrew = ('[kaiHEN’s official instructions](https://github.com/ouncebrew/kaihen#starting-it)' if native else
                '[Nintendo Homebrew’s Switch setup guide](https://switch.hacks.guide/)')
    ready = ('A compatible **native homebrew** setup. Native means made for Switch 2. It must already open the native Homebrew Menu.' if native else
             '**Atmosphère custom firmware** and **Homebrew Menu**, already working. Custom firmware lets your console run community-made apps.')
    intro = ('This is an early test version. Bugs can interrupt play. Read the audio warning below before you begin.' if native else
             'Let’s put Sonic Riders on your console. Take one small step at a time. You do not need to write any code.')
    prerequisites = [
        f'Check the console’s name. This booklet is for **{platform}**. ' +
        ('If your console is an original Switch, Switch Lite, or Switch OLED, use the other booklet.' if native else 'Switch, Switch Lite, and Switch OLED use this stable package. If the console says Switch 2, use the other booklet.'),
        f'Check that homebrew already works: {ready}',
        f'If homebrew is new to you, start with {homebrew}. Finish that setup before coming back here. The Sonic Riders installer does not set up homebrew.',
        'Use a **64-bit Windows 10 (1803 or newer) or Windows 11 computer**. Connect it to the internet. Windows 11 is recommended. A Mac, phone, or Chromebook cannot run this Windows installer.',
        ('Make at least **8 GB of free space** on the computer. Extra room is helpful if your disc needs converting.' if native else 'Make at least **4 GB of free space** on the computer. Extra room is helpful if your disc needs converting.'),
        ('Have the **SD card already used by your working Switch 2 homebrew setup**. Leave at least **2 GB free**, plus room for any texture packs. Have a reader that can read that card, including microSD Express if that is what your setup uses.' if native else 'Have the **microSD card already used by your working Switch homebrew setup**. Leave at least **2 GB free**, plus room for any texture packs. Have an SD card reader for your computer.'),
        'Have your own **Sonic Riders for GameCube, USA, revision 0** disc image. The next page explains this file.',
        'Keep the computer awake and plugged in while it builds. Allow **10–40 minutes** for a first install. Download speed and computer speed can change this time.'
    ]
    if native:
        prerequisites.insert(3, 'The recorded console test used **firmware 23.0.1, kaiHEN 20261117.1 through Seedsow, and OunceShell**. This is one tested setup, not a promise for every console. The [Seedsow project’s setup instructions](https://github.com/Malev0/seedsow-fs-kaihen#setup-once) describe that entry route and its extra requirements. Do not update firmware just to match these numbers. Check those projects’ current compatibility instructions.')
    chapters = [chapter('ready','Your little starting checklist',prerequisites,'ready',
        f'Illustration: {platform}, Windows computer, prepared SD card, card reader, internet and your own game disc.',
        'You have a working homebrew menu, a Windows computer, free space, and your own game image.',tone='old',
        note='A homebrew menu is a list of community-made apps. If you cannot open it yet, finish the linked homebrew setup first. Do not format a card that already has your files on it.'),
      chapter('disc','Make your own game file',[
        'Find your own **USA GameCube Sonic Riders** disc. This is the original Sonic Riders, not Sonic Riders: Zero Gravity.',
        'A **disc image** is one file holding a copy of a disc. A full image usually ends in `.iso`. Copying a disc to this file is called **dumping**.',
        f'If you need to dump it, open the [illustrated Wii dumping walkthrough]({WII}). It uses **CleanRip**, a disc-copying homebrew app. Follow its **GameCube** section. You need a Wii that can read GameCube discs and a working Homebrew Channel. The Wii mini, ordinary Wii Family Edition, and Wii U’s Wii mode cannot read these discs.',
        'Let CleanRip finish. Copy the resulting full `.iso` from its storage device to a folder on your computer. Keep the original file safe. Follow the dumping guide’s file-joining instructions if your dump is split into parts.',
        'Already have a dump? You can select a clean `.iso` or `.gcm`. A clean Dolphin `.rvz` or `.wia` also works. The installer converts it to `.iso` for you. Your original file stays unchanged.',
        'Use a clean, full copy. European, Japanese, modified, trimmed, incomplete, GCZ, and CISO images are not accepted. An RVZ made with **Remove Junk Data** is not a full copy. Renaming a file cannot turn it into the right game.'
      ],'disc','Illustration: your own USA GameCube disc becomes a full ISO file on your computer; the installer checks it.',
      'Your own full game image is on the computer. You know where you saved it.',tone='old',
      note='The installer contains no game. Use your own disc and follow the rules where you live. We do not provide game downloads.',
      extra='**For helpers:** accepted disc ID `GXEE8P`, revision `0`; full ISO size `1,459,978,240 bytes`; SHA-1 `ce0cecf57034bef8bab7d6f588ce92c85a849613`. The installer checks this fingerprint automatically. A filename alone does not prove that a dump is supported.'),
      chapter('download','Pick the right download',[
        f'Open the [{platform} release page]({release}). GitHub is the website that holds this project’s downloads. You can read the page and download without an account.',
        f'Check the heading. This page should say **{("Switch 2 Alpha 2" if native else "v0.8.7")}**. ' + ('The **Pre-release** label is expected. Alpha means it is still being tested.' if native else 'This is the stable release for this booklet.'),
        'Scroll down to **Assets**. Assets means the downloadable files. If the list is folded up, click **Assets** to open it.',
        f'Click **`{archive}`**. You can also use the big download button below.',
        'Wait for the download to finish. Do not choose **Source code (zip)** or **Source code (tar.gz)**. Those are for developers and do not contain the ready-to-use Windows installer.'
      ],'release',f'Illustration of the GitHub download choices: select {archive} under Assets, not Source code.',
      f'The downloaded filename is exactly `{archive}`.',
      note=('Keep this alpha package separate from stable downloads. Its updates are manual.' if native else 'A PC, Android, or Linux setup ZIP will not install the Switch version. The separate Switch 2 ZIP is for a different console.')),
      chapter('extract','Unpack the whole ZIP',[
        'Open **File Explorer**. It is the yellow folder app on the Windows taskbar, at the bottom of the screen.',
        f'Click **Downloads** in the left column. Find `{archive}`. If you chose a different download location, open that folder instead.',
        'Click the ZIP with the **right mouse button**. Choose **Extract All…**. A ZIP is a packed box of files. Extracting opens the box and puts all its files in a normal folder.',
        f'In the extraction window, choose a short place you can find again, such as `{local}`. Use English letters and numbers in the path. Spaces work, but accented characters in the path can stop the tools.',
        'Click **Extract**. Wait until it finishes. Keep all the extracted files together.',
        f'Open the extracted folder. Open `{extracted}` if there is one more folder inside. Stop when you can see `{exe}`, `setup`, and `tools` together.'
      ],'extract','Illustration: right-click the downloaded ZIP, choose Extract All, then open the normal extracted folder.',
      f'You see `{exe}` beside the `setup` and `tools` folders. The address at the top does not end in `.zip`.',
      note=f'Do not double-click the installer while browsing inside the ZIP. Copying only the EXE is not enough. [Windows extraction help]({ZIP_HELP}).'),
      chapter('card','Connect the SD card',[
        'Quit any running game. Power the console fully off using its power menu. Sleep mode is not power off. Follow your homebrew setup’s shutdown instructions.',
        'Remove the prepared SD card. Put it in your computer’s card reader. If your computer has no matching slot, use a suitable USB reader.',
        'Open File Explorer. Click **This PC** in the left column. Look for the card under **Devices and drives**. A drive is a storage device shown by Windows.',
        'Open the card. Its **root** means its very top folder. You should be looking at the existing `switch` folder, alongside your other homebrew files. Do not open `switch` yet.',
        'If a working homebrew card has no `switch` folder yet, make one at this top level: right-click an empty space, choose **New → Folder**, and type exactly `switch`. Leave the other folders alone.',
        'If Windows asks you to format the card, click **Cancel**. Formatting erases files. Check that the reader supports the card and that it is the right card.'
      ],'card','Illustration: fully power off, put the prepared SD card in a reader, then open the top level in This PC.',
      'Windows can show the existing homebrew files on your card. You have not formatted or erased it.',tone='sd',
      note=('This booklet uses a card reader. An existing native OunceShell transfer setup is an advanced alternative. It is not needed for this route.' if native else 'No reader? A working hekate setup can expose the card over USB: **Tools → USB Tools → SD Card**. Connect a USB-C cable to the PC. Follow [hekate’s own USB instructions](https://github.com/CTCaer/hekate). This is an optional alternative to removing the card.')),
      chapter('open','Open the installer',[
        'Go back to the normal folder you extracted on your computer.',
        f'Double-click **`{exe}`**. Double-click means press the left mouse button twice quickly.',
        'Windows may hide the `.exe` ending. In that case, look for the same name with **Application** in the Type column. Do not open the command-line BAT file for this picture-booklet route.',
        f'Check the installer window. It should identify **{("Switch 2 Alpha installer" if native else "Switch installer")}**.',
        'If Windows blocks the program, first check that the ZIP came from the exact project release linked above. This installer is unsigned. If you cannot verify the download, stop and ask someone you trust to help. Do not turn off Windows security.'
      ],'installer',f'Actual published {platform} installer. Numbered markers identify Select disc, SD card choices, optional settings and Build and Install.',
      f'The {platform} installer is open. You are still on the computer.',
      note='The colorful picture across the top is part of the real installer. Your window may be a different size. You can make it larger by clicking the square at the top right.'),
      chapter('choose-disc','Tell it where your game file is',[
        'Click the pink-marked **Select disc…** button shown in the picture.',
        'In the file window, open the folder where you saved your own game dump. Single-click the `.iso`, `.gcm`, `.rvz`, or `.wia` file.',
        'Click **Open**. This selects the file for the installer. You do not need to double-click or mount an ISO in Windows.',
        'Look below **Select disc…**. Your selected file’s name should now appear. The installer performs the full game checks when you start the build.'
      ],'select-disc','Actual installer detail: Select disc button and the place where the selected game filename appears.',
      'The installer shows the game image you chose. It no longer says “No disc image selected”.'),
      chapter('choose-card','Tell it where the SD card is',[
        'Find **2. SD CARD** in the installer.',
        'Click **Refresh** if you inserted the card after opening the installer.',
        'Click the small downward arrow on the SD card box. Choose your card if it is listed.',
        'If your card is missing from that list, click **Other…**. In the folder picker, choose **This PC**, then your SD card’s top folder. Click the folder picker’s confirmation button. Do not choose the folder holding the installer.',
        'Check that the card you picked is the same card you opened in the previous page. For a first install, keep **Copy the disc image too** ticked. This puts the game’s data beside the program.',
        f'If you want to copy files later, choose **{("Keep SD-ready files on this PC" if native else "Keep build on this PC")}** instead. This makes files on the computer only. Use the manual-copy page below afterward.'
      ],'select-card','Actual installer detail: SD dropdown, Refresh and Other buttons. The copy-disc checkbox appears after selecting a destination.',
      'For the easy route, the installer shows your SD destination and “Copy the disc image too” is ticked.',tone='sd',
      note='Automatic detection is only a suggestion. Always check the card yourself. A missing card in the list does not mean you need to format it.'),
    ]
    if native:
        options = [
            'Find **3. SHADER COMPILER LICENSE**. Click **Read shader compiler license**. Read the terms before deciding.',
            'If you agree, tick **I accept the Android SDK License**. This is needed for a tool that prepares the graphics. It does not install an Android game on your console.',
            'The compiler download is about **1 GB**, in addition to other tools. Leave the computer online.',
            '**4. GAME ICON** uses the project’s game icon. There is no icon file to choose here.'
        ]
        option_note='Fresh installs use 2x internal resolution, normal 60 FPS and no automatic exit timer. Leave the default settings alone for the first launch. Unlimited rendering is experimental. Changing resolution while the game is running is disabled in this alpha.'
    else:
        options = [
            'Leave **Use my own picture** unticked for a first install. The installer will use its default game icon. If you choose your own picture later, tick it and use **Pick picture…**. A square picture at least 256 × 256 works best.',
            'Leave **Make a HOME-screen icon** unticked for this first install. It is optional. The game will launch from Homebrew Menu without it.',
            'If you tick the HOME-screen option later, **Pick prod.keys…** needs your own console’s keys. The option also needs separate console installation steps. Follow the [advanced HOME icon instructions](INSTALL.md#optional-a-sonic-riders-icon-on-the-home-screen) with an experienced helper. Never download or share console keys.',
            'Leave **4. TEXTURE PACKS (OPTIONAL)** alone for the first test. Texture packs replace how the game looks. They are not needed to play.',
            'Later, **Add texture pack…** accepts your downloaded ZIP, 7z, or RAR pack. **Add a pack folder…** accepts an unpacked folder. Read the [texture pack instructions](https://github.com/teclilla18/sonic-riders-recomp#hd-texture-packs-pc-and-switch) first. Large packs need extra SD space and can pause the game when a new texture loads.'
        ]
        option_note='The first-install route needs only your game image, the right SD card, and Copy the disc image too. Optional pictures, HOME icons and texture packs can wait.'
    chapters += [chapter('options','A few choices before you build',options,'options',
        'Actual installer detail: '+('shader compiler license and universal game icon.' if native else 'optional custom picture, HOME-screen icon and texture-pack buttons.'),
        'Your required choices are complete. Optional extras can wait.',note=option_note),
      chapter('build','Press the big button and wait',[
        'Click **BUILD AND INSTALL** at the bottom right. If it is dimmed, read the message at the bottom. It tells you which required choice is missing.',
        'Leave the installer open. It checks your image, downloads its tools, and builds the program from your own disc. **Build** means make the program that your console can run.',
        'Watch the progress messages. Some steps take several minutes. A changing log is a sign that it is working. Keep the internet connected and the SD card plugged in.',
        'Do not press **Stop** unless you want to cancel. Do not close the window, unplug the card, or put the computer to sleep while files are being written.',
        'Wait for **Done** or **INSTALLATION COMPLETE**. If you see **INSTALL FAILED**, go to the help section. Having a folder named `build` does not by itself mean the build succeeded.'
      ],'build','Illustration: checks, tools, build and copy lead to Done; a failed build leads to setup.log.',
      'The installer says the installation completed. There is no failure message.',
      note='You do not need to install Python, Visual Studio, devkitPro or another development kit yourself. The installer fetches its own checked tools.'),
      chapter('output','Meet Open Output Folder',[
        'After success, click **Open Output Folder** in the bottom action bar. The button stays there when you resize the window.',
        f'File Explorer opens a folder **on your computer**: `{output}/`. It opens this local folder even if you already disconnected the SD card.',
        ('Look for `sonicriders-switch2.nro`, `build-info.json`, `env.txt`, and `settings.ini`. The `.nro` is the console app. The JSON file identifies the matching build. The two text files hold startup defaults and settings.' if native else 'Look for `sonicriders.nro`. An `.nro` is a homebrew app that your console can launch.'),
        'If the installer already copied to the correct SD card and reported success, continue to **Check the finished SD card**. Opening this folder does not copy anything by itself.'
      ],'completion','Actual completed installer screenshot: Done, local output path and Open Output Folder highlighted; build log hidden for privacy.',
      ('The local folder contains the native NRO and its matching build-info.json.' if native else 'The local folder contains sonicriders.nro.'),
      note='The local output folder is not your SD card. If you chose to keep the build on the PC, you still need the next page.'),
      chapter('manual','Copy by hand, if you chose PC-only',[
        'Use this page only if the installer did not copy to your SD card. Keep the installer’s successful output folder open.',
        'Open a second File Explorer window. Open **This PC**, then your SD card.',
        f'Open the card’s `switch` folder. If `{app}` is missing inside it, right-click an empty space, choose **New → Folder**, and name it exactly `{app}`.',
        ('In the local output folder, select `sonicriders-switch2.nro`, `build-info.json`, `env.txt`, and `settings.ini` for a **fresh** install. Hold **Ctrl** while clicking each file. Press **Ctrl+C** to copy.' if native else 'In the local output folder, single-click `sonicriders.nro`. Press **Ctrl+C** to copy it.'),
        f'Open `{sdpath}` on the card. Press **Ctrl+V** to paste. Wait for the copy to finish.',
        'Copy your own **full ISO or GCM** into that same folder. An RVZ or WIA cannot be used directly on the console. If the installer converted one, use the generated `.iso` from the extracted installer’s `baserom` folder.',
        'Keep just one supported game image in this game folder. Its name may differ from the example. It must really end in `.iso` or `.gcm`, not `.rvz`, `.zip`, or `.iso.iso`.'
      ],'copy',f'Illustration: copy the console app and full game image into {sdpath}, not the whole Windows installer folder.',
      f'The app and your full disc image sit together inside `{sdpath}`.',tone='sd',
      note=('For an existing install, copy only the new NRO and matching build-info.json. Keep the SD card’s own env.txt and settings.ini. The SD-ready folder does not include your disc image.' if native else 'Do not copy the Windows EXE, the ZIP, the tools folder, or the whole build folder to the SD card. If adding texture packs later, follow the separate mod instructions.')),
      chapter('folders','Check the finished SD card',[
        f'On the card, open `switch`, then `{app}`. Compare it with the picture below.',
        f'Check the exact app name: **`{app}.nro`**. Do not rename another platform’s app to this name.',
        'Check that your own full `.iso` or `.gcm` is in the **same folder** as the NRO. The example uses `SonicRiders.iso`; the installer keeps your ISO’s original name.',
        'If Windows hides file endings, use **View → Show → File name extensions** on Windows 11. On Windows 10, open **View** and tick **File name extensions**. Now check that the app does not end in `.nro.nro`.',
        'Leave your other homebrew folders alone. This picture shows only the part that matters to Sonic Riders.'
      ],'folders',f'Illustrated correct SD tree for {platform}, plus a crossed-out extra nested switch folder.',
      f'The path is exactly `{sdpath}{app}.nro`, with the full image beside it.',tone='sd'),
      chapter('eject','Unplug the card safely',[
        'Wait for all copy windows to finish. Close any files or Explorer windows using the card.',
        'If the installer has your SD card selected, click **Eject SD card**. Wait for **Safe to remove the SD card now.**',
        'Or use Windows: click the small **up arrow** near the clock at the bottom right. Right-click **Safely Remove Hardware and Eject Media**. Choose **Eject** for your card or reader.',
        'Wait for Windows to say the hardware can be removed. If it says the card is in use, close the program using it and try again. Do not pull it out during a copy.',
        'Take the card out of the reader. With the console fully powered off, put the card back in it. Start your usual working homebrew setup.'
      ],'eject','Illustration: completed file copy, Eject, Safe to remove, then reinsert the card in the powered-off console.',
      'The card is back in the console after a safe eject.',tone='sd',
      note=f'[Microsoft’s illustrated safe-removal help]({EJECT_HELP}). ' + ('If you used a native file-transfer app instead, finish its transfer and disconnect using that app’s instructions.' if native else 'If you used hekate’s USB storage instead, eject in Windows before ending USB storage in hekate and booting your usual custom firmware.')),
      chapter('launch','Let’s open Sonic Riders',[
        ('Start your already-configured **native Switch 2** homebrew environment.' if native else 'Start the console in your already-configured **Atmosphère** environment.'),
        ('On HOME, highlight a **native Switch 2 game** that your loader supports. Hold the controller’s **R shoulder button** while opening it. Keep holding R until the native Homebrew Menu opens.' if native else 'On HOME, highlight an installed game you can launch. Hold the controller’s **R shoulder button** while opening the game. Keep holding R until Homebrew Menu opens. If it asks for a user, keep R held while choosing one.'),
        ('Follow the current loader instructions if the first launch still opens the game. kaiHEN’s instructions say the second launch and later open hbmenu. Close the native game normally and repeat the hold-R launch when instructed.' if native else 'Use this game-launch route, called **full-memory mode** or **title override**. Opening Homebrew Menu through **Album** gives less memory and is not the route for this game.'),
        'In Homebrew Menu, use the stick or D-pad to highlight **Sonic Riders**. Press **A**.',
        'Wait for the game to start. You should reach the intro or title screen. Follow the game’s on-screen prompts. It may ask to create a memory-card save on the first run.',
        'When you reach the main menu, you have completed the install. Try a short race before adding optional packs or changing many settings.'
      ],'launch',f'Illustration, not console capture: hold R over {("a native Switch 2 game" if native else "an installed game")}, choose Sonic Riders in Homebrew Menu, then reach the title screen.',
      'Sonic Riders reaches its title screen and menu. A successful PC build alone is not this checkpoint.',
      note=('This alpha has no native HOME-screen forwarder. The loader determines how hbmenu opens; the native-game hold-R route is the one recorded in the project’s console testing.' if native else 'The R button is the small right shoulder button, not ZR and not the right stick. A separate HOME-screen icon is optional and is outside this first-install route.')),
      chapter('play','Check the version, then enjoy',[
        'In the game, hold **Minus (−)** for about half a second. This opens the port’s settings menu.',
        f'Open **About**. Check the version/build information against **{version}**. If an old version is still shown, follow **Update without losing your progress** below.',
        ('Keep normal **60 FPS** selected for a first test. **Unlimited rendering** is experimental. The number 60 is a target, not a guarantee for every scene or setup.' if native else 'For a first test, keep the normal settings. Stock-clock consoles can run races at 30 FPS. This booklet does not require overclocking.'),
        'To quit normally, open the Minus menu, choose **Quit Game**, and confirm. Wait to return to Homebrew Menu. Do not remove the SD card while the game is saving.'
      ],'play','Illustration: hold Minus, open About to check the version, and use Quit Game for a normal exit.',
      'The version/build is the one you installed. You can open settings and quit normally.'),
      chapter('update','Update without losing your progress',[
        'Quit Sonic Riders normally before you update. Connect the SD card to your computer using the same safe steps as before.',
        f'**Back up first.** Copy the whole `{sdpath}` folder from the SD card to a safe folder on your computer. A backup is a spare copy. Keep it until you know the update works.',
        f'Open the [{platform} release page]({release}). For a future update, check for a newer release for **this same platform**. Download its matching setup ZIP.',
        'Extract the new ZIP to a **new normal folder** on the PC. Open its installer. Select your same own game image and the same SD card root.',
        'Build and install. Keep the existing full game image if it is already verified on the card; **Copy the disc image too** can be unticked for that case.',
        ('For a manual update, copy **only** the new `sonicriders-switch2.nro` and its matching `build-info.json` into the existing game folder. Replace that pair together. Do not overwrite your SD card’s `env.txt` or `settings.ini` with fresh defaults.' if native else 'For a manual update, copy **only** the new `sonicriders.nro` into the existing game folder and replace that file.'),
        'Keep `saves/`, `settings.ini`, `mods/`, `env.txt` if present, and cache files. Your texture packs live under `mods/`. Do not delete the old game folder or replace it with an empty folder.',
        'Eject safely. Launch again. Open **Minus → About** to check the version/build. Also check that your save and chosen settings are still there.'
      ],'update','Illustration: back up the game folder, replace only the program files, and keep saves, settings, texture packs and caches.',
      'The new version/build appears, and your progress, settings and packs are still present.',tone='sd',
      note=('Alpha updates are manual. Do not use the stable automatic updater for this native target. To roll back, restore your backed-up NRO and its matching build-info.json together; retain your user data.' if native else 'The stable installer also has **See what’s new → UPDATE NOW** on supported installed versions. Connect the same SD card first. The manual route here is useful if that updater fails. Issue #31 reports a Switch Lite still showing 0.1.0 after an update; its cause is unresolved. Do not erase your save to try to fix a version number.'))
    ]
    tree = f'''SD card (top folder)
└── switch/
    └── {app}/
        ├── {app}.nro
'''
    if native:
        tree += '        ├── build-info.json\n        ├── env.txt\n        ├── settings.ini\n'
    tree += '        └── SonicRiders.iso   ← your full image; its name may differ\n'
    later = f'''{sdpath}
├── saves/             ← your game progress; keep this
├── settings.ini       ← your choices; keep this
├── mods/              ← texture packs/mods, if added; keep this
└── cache files        ← may appear after playing; keep these
'''
    trouble = [
      ('wrong-zip','My download does not contain the right installer','download',
       f'Check the full ZIP name: `{archive}`. Open the release page and choose that asset. **Source code (zip)** is not the setup package. Keep all extracted files together.'),
      ('inside-zip','The installer cannot find setup files','extract',
       'Close it. Right-click the original ZIP and choose **Extract All…**. Open the normal extracted folder. The EXE, `setup`, and `tools` must be together. Do not run a lone EXE copied out of the ZIP.'),
      ('bad-image','It says the game image is wrong','disc',
       'Read the exact message. Only the clean USA GameCube disc, revision 0, is supported. Select the full correct dump. For a checksum or incomplete-copy error, dump your own disc again using CleanRip’s checks. A different filename cannot fix different contents. RVZ/WIA must preserve the original disc data. See the full-image fingerprint on the disc page.'),
      ('missing-file','The app or disc file is missing','folders',
       f'Wait for a successful build first. Open the output folder and copy `{app}.nro` to `{sdpath}`. Put your full ISO/GCM beside it. Show file name extensions to catch `.nro.nro` or `.iso.iso`. RVZ stays on the PC; copy the converted ISO from `baserom` instead.' + (' Also copy the matching `build-info.json`.' if native else '')),
      ('nested','My folders look different','folders',
       f'The card needs `{sdpath}{app}.nro`. An extra `switch` inside `switch` is wrong. So is copying the Windows setup folder to the card. Open the correct destination and copy only the files shown in the finished-tree picture. Preserve the other apps already on the card.'),
      ('not-listed','Sonic Riders is not in Homebrew Menu','launch',
       f'Check that you are using the correct platform’s working Homebrew Menu. Check for `{app}.nro` directly in `{sdpath}`. Eject safely after correcting the files, then reopen the menu. A ZIP or Windows EXE will not appear as a console app.'),
      ('no-card','The SD card is not found','card',
       'Check that File Explorer can open the card. In the installer, press **Refresh**, then try **Other…** and pick its top folder. If Windows asks to format it, cancel. Check the reader and your existing homebrew setup before continuing.'),
      ('build-error','The installer says INSTALL FAILED','build',
       'Keep the window open and read the last error. Find `setup.log` beside the installer EXE. If it reports insufficient space, free the stated space. If it reports a download failure, restore your connection and retry. If it reports unsupported path characters, extract to the short English-letter PC folder shown earlier. If a hash check fails, do not bypass it. Save the log and report the error if retrying does not fix it.'),
      ('black-screen','I get a black screen or a crash','launch',
       ('Check the native Switch 2 package, native Homebrew Menu, full ISO/GCM, and exact folder first. Do not use the original Switch NRO or rename it. If those match, preserve the logs and report it. Alpha compatibility is limited; there is no verified universal black-screen fix.' if native else 'First check that you launched Homebrew Menu by holding R while opening an installed game. Album mode has too little memory. Next check the full ISO/GCM and exact folder. These checks fix setup mistakes; they do not establish the cause of every crash. If it still fails, preserve the logs and report it.')),
      ('known','Known problems that are still open','play',
       ('**Unresolved:** Race Select (Free Race / Time Attack / World Grand Prix) can develop severe persistent audio distortion, sometimes after a freeze. Turning Unlimited off and leaving the menu did not recover the reported session. Quitting and relaunching may help, but it is not a proven cure. Missing music/announcer audio, stutters and slow startup are also known alpha issues. Keep normal 60 FPS for the first test. Save logs before relaunching. Do not treat an audio problem as proof that you installed it incorrectly.' if native else '**Unresolved:** rare hangs on **Now Loading** are tracked in [issue #6]('+REPO+'/issues/6). Texture packs can briefly freeze the original Switch when new textures load. Test a fresh install before adding packs. There is no verified fix for every loading hang. An old version still appearing after an update is reported in [issue #31]('+REPO+'/issues/31); its cause is still being investigated.')),
      ('old-version','About still shows the old version','update',
       f'Compare the new local NRO with the file in `{sdpath}`. Make sure you copied to the card you actually use. Follow the manual update route, then launch that app again and check **Minus → About**. Keep your backup and save. If the version remains old, report both the downloaded ZIP name and the displayed version. Do not assume this identifies the cause of issue #31.')
    ]
    logs = [
        'For an installer problem, open the normal extracted setup folder. Find **`setup.log`** beside the installer EXE. Open it in Notepad. Save a copy before retrying.',
        f'For a console problem, quit normally if possible. Before launching again, copy **`sonicriders.log`** and **`sonicriders.prev.log`**, if present, from `{sdpath}` to the computer.' + (' Also keep `lifecycle-*.log` files if present. The `*` stands for the different numbers in their names.' if native else ''),
        'Read the copies before sharing. Replace personal paths, names, IP addresses or other private details with `[removed]`. Do not remove the error messages.',
        f'Open the [bug report form]({REPO}/issues/new?template=bug_report.yml). A free GitHub account is needed to post a report. Reading guides and downloading do not need one.',
        f'Write **{platform}**, the ZIP filename, version/build shown in About, console model, firmware, homebrew loader/version, and exactly what you clicked or played. Say handheld or docked, stage, character, gear, player count, settings and any known clock settings. If you do not know a number, say “unknown”.',
        'Add the error text and the cleaned log copies. Say what you expected, what happened, and whether you can make it happen again. A cropped photo of the error can help; hide private details.',
        'Never attach your ISO/RVZ, NRO, saves, console keys, passwords, generated code or game assets. Those are not needed for a useful public report.'
    ]
    return dict(mode=mode,native=native,platform=platform,edition=edition,version=version,tag=tag,archive=archive,
                exe=exe,extracted=extracted,app=app,sdpath=sdpath,output=output,release=release,download=download,
                other=other,other_md=other_md,other_name=other_name,intro=intro,chapters=chapters,
                tree=tree,later=later,trouble=trouble,logs=logs,homebrew=homebrew)
