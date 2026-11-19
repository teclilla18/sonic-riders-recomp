# Sonic Riders Recomp — Switch 2 Alpha 2

**Known serious limitation:** Race Select (Free Race / Time Attack / World Grand Prix) can develop persistent
severe audio distortion, with a freeze at onset. Normal 60 FPS and leaving the menu did not recover the
reported accumulated state. Relaunching may help, but is not a guaranteed fix. Keep 60 FPS as the default;
Unlimited rendering is experimental and may exacerbate stress. This is an alpha, not a stable native release.

Includes applicable v0.8.7 Riders Plus/HUD/finished-level fixes, Unlimited as an opt-in experiment, and the
universal game icon. User accepted build0d995f3def88 for Super balance, high-ring finish, icon and2P Fit HUD.
Similar audio/stall defects predate these changes; no severe newregression is established, but exact cause
and resource/PCM/underrun behavior remain unresolved. Diagnostic logs are preserved locally.

Experimental community compatibility, bug, performance and stress testing.
[Download Alpha 2](https://github.com/teclilla18/sonic-riders-recomp/releases/tag/v0.8.7-switch2-alpha.2). This experimental pre-release is separate from stable v0.8.6.
No compiled game or Nintendo/SEGA game data is distributed.

## What has been tested

Runtime baseline: accepted `80304ca` / `0d995f3def88`, retaining the earlier scheduler wakeup and asynchronous logging fixes.
One physical Switch 2: firmware23.0.1, kaiHEN20261117.1 launched through Seedsow,
native-game hold-R Homebrew Menu flow, OunceShell installed. These environment
details are the tester's report, not a promise for other setups or firmware.
Handheld mode0; clocks unrecorded. No universal performance or60fps guarantee.
The user accepted tested gameplay, input, graphics, music/sound, overlay, aspect,
bloom and tried enhancements; manual shutdown, hbmenu return and relaunch work.
Other stages/story, extended play, multiplayer, docking, sleep/resume and other
homebrew environments require testing. Windows Sandbox checks installation and
building only; they cannot establish physical Switch 2 functionality.

Higher-FPS interpolation is excluded. Its separate experimental build froze at
Player Entry after selecting Sonic with Chaos Emerald gear (user report). It is
preserved separately for investigation. Alpha uses the accepted renderer.
Known issues: intermittent stuttering or temporary freezes, music/announcer dropout, temporary audio distortion and minor selection audio glitches. Relaunching has restored missing music in tests; not all symptoms are fixed. Recent two-race test had no selection freeze: first race had two temporary stalls and recoverable distortion, second was otherwise flawless. Further fixes are deferred after Alpha 2. Startup/loading can hitch for several seconds; logs identified synchronous
pipeline-cache saving. Do not delete caches as a routine update step.

## Install on Windows

1. Download **SonicRiders-Switch2-Setup-Alpha2.zip** from the dedicated alpha
   pre-release, not the ordinary latest/stable download. Extract the whole ZIP.
2. Run **Install Sonic Riders (Switch2).exe**. It uses the existing Switch
   installer's interface. Pick your own clean USA GXEE8P revision0 ISO/GCM or
   RVZ/WIA. Existing ISO hash checks and RVZ conversion are reused. Originals
   remain unchanged; converted images and generated game code stay local.
3. Accept the shader compiler's Android SDK License after reading it. Only
   glslc from the pinned Android NDK is used; no Android app is built. Expect
   about1GB for that download plus other tools, and at least8GB free locally.
   The setup downloads and SHA-256 verifies dependencies; no preinstalled tools
   or administrator installation of a development environment is required.
4. Pick the SD card's root, or choose no copy for manual FTP transfer. The native
   target uses `/switch/sonicriders-switch2/`; original Switch installs are separate.
5. Put your own validated disc image alongside the NRO. Eject the card safely.
   Launch native homebrew with full memory through your compatible loader;
   the tested flow holds R while launching a native Switch 2 game.

Command-line fallback from the extracted folder:

```powershell
& '.\Install Sonic Riders (Switch2) - command line.bat' -Iso '<your image>' -NoSd -Yes -AcceptAndroidSdkLicense
```

Use `-Sd '<SD root>' -CopyIso yes` instead of `-NoSd` to copy to SD. The underlying
entry point is `setup/setup.ps1 -Target switch2` with the same arguments.
Local output is `build/switch2/sonicriders-switch2.nro`; an SD-ready folder is
`build/switch2/sd/switch/sonicriders-switch2/`. For manual FTP, copy that folder's
NRO/manifest to `/switch/sonicriders-switch2/` and your own image there. Copy its
default env.txt only for a fresh installation; retain an existing environment.

## Controls and exit

Left/right sticks map to GameCube control/C-stick, face buttons map by name,
Plus is START, ZL/ZR are GameCube L/R, and R is GameCube Z. The D-pad maps by
direction. See the overlay Controls tab for the layout.
Hold Minus about0.5seconds for the overlay. Choose **Quit Game**, then confirm,
to shut down and return to the Homebrew Menu. Fresh installs use **2x internal resolution**, target 60 FPS and
write `SR_SWITCH2_FRAMES=0`, meaning no automatic exit. Existing env/settings
are retained. **Live resolution changes are disabled** in this alpha because they
could leave the overlay frozen. The alpha retains the accepted Auto/30/60 menu, with Unlimited rendering available only as an explicitly experimental option.

## Update and rollback

Alpha updates are manual. Download the dedicated Switch2 package, extract to a
new folder, and rerun the installer. The Switch2 compiler/SDK/build work folder
is isolated from other platforms; common pinned download helpers are reused.
Only native NRO/build metadata are replaced. Existing env.txt, settings.ini,
saves, mods and shader caches remain. Avoid copying entire old test ZIPs or
replacing customized configuration. There is no native HOME-screen forwarder
in this alpha. The original Switch installer and folder are unaffected.

## Report a bug

Use the [bug report form](https://github.com/teclilla18/sonic-riders-recomp/issues/new?template=bug_report.yml). State Alpha 2 and the build ID from startup,
firmware, loader/version, handheld/docked mode, known clocks, track/gear/player
count/settings, exact steps, and whether it recurs. For performance, distinguish
warm from cold cache and do not compare hardware against PC/emulator timings.
Useful SD logs: `sonicriders.log`, `sonicriders.prev.log`, and
`lifecycle-NNNNNN.log` under `/switch/sonicriders-switch2/`. Review logs for
personal paths before sharing. Never attach ISO/NRO/generated code, extracted
assets, saves, console keys or credentials. If frozen, preserve logs before
relaunching; do not assume a clean exit occurred. System HOME/volume response
alone does not prove the game's workers remain healthy.

## Credits and transparency

Thanks to ouncebrew/ounce-devkit, libounce, OunceVK and kaiHEN developers; Seedsow
and OunceShell developers; devkitPro/devkitA64/switch-tools; Google shaderc/NDK;
Dear ImGui; and decomp-toolkit contributors. Exact pinned URLs, hashes and SDK
component revisions are in [the release's toolchain lock](https://github.com/teclilla18/sonic-riders-recomp/blob/v0.8.7-switch2-alpha.2/setup/toolchain.json), [SDK dependency lock](https://github.com/teclilla18/sonic-riders-recomp/blob/v0.8.7-switch2-alpha.2/diagnostics/switch2/dependencies.json),
and the generated release-metadata.json. Dusklight and Aurora informed native
platform patterns; their game code/dependencies are not linked into this port.
See [the release's third-party notices](https://github.com/teclilla18/sonic-riders-recomp/blob/v0.8.7-switch2-alpha.2/diagnostics/switch2/THIRD_PARTY_NOTICES.md) and the repository licenses/credits.
SEGA/Nintendo own Sonic Riders and associated trademarks; this is an unofficial
fan project. No game data is included. Development and documentation are
LLM-assisted (Claude Code/Codex), with human physical hardware feedback. Source,
local build checks, cold installer validation and hardware acceptance are
separate forms of evidence; alpha testing is intended to expose remaining bugs.
