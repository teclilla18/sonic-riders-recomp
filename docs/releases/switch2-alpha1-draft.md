# Sonic Riders Recomp — Switch 2 Alpha 1

**Experimental native Switch 2 pre-release** for community bug reporting,
hardware compatibility, performance and stress testing. Playable, not bug-free.

Download **SonicRiders-Switch2-Setup-Alpha1.zip** and its SHA-256 checksum.
Windows GUI/CLI setup builds locally from your own validated USA GXEE8P rev0
ISO/GCM or supported RVZ/WIA. No game data, compiled NRO, extracted assets,
keys, saves, logs or caches are distributed. Read the [alpha guide](../SWITCH2-ALPHA.md).

Baseline: hardware-tested **c8c06fdda996**, source **fa2ec57**, with scheduler
wakeup and bounded asynchronous logging fixes. Multiple consecutive races no
longer reproduced the selection freeze; permanent elimination is not claimed.
Latest two-race report: first had two temporary stalls and recoverable audio
distortion; second otherwise flawless except a minor selection audio glitch.
Fresh default **2x, 60 FPS, no automatic exit**. Live resolution controls are
disabled. Existing settings/saves/caches are retained. Hold Minus~0.5s, choose
Quit Game and confirm to return to hbmenu. Higher FPS is excluded.

**Known issues:** intermittent stuttering/temporary freezes, music and/or
announcer dropout, temporary audio distortion, and startup/cache-save hitches.
Relaunching restored missing music in prior tests. Announcer can disappear
independently of stutter. These symptoms remain for community testing; no
claim that all share a cause or are fixed. Latest logs preserved for later work.

Reported hardware environment: firmware23.0.1, kaiHEN20261117.1 via Seedsow,
native-game hold-R flow; handheld, clocks unrecorded. Other firmware/loaders,
stages/story/multiplayer/docking/sleep and long-run stability need testing.
Windows Sandbox validates installer/build behavior, not console functionality.
Please report build ID, environment/mode/clocks, reproduction steps and reviewed
runtime/lifecycle logs. Never attach your ISO/NRO/assets/saves/keys.

Credits: ouncebrew/ounce-devkit, libounce, OunceVK, kaiHEN, Seedsow/OunceShell,
devkitPro, shaderc/NDK, decomp-toolkit and Dear ImGui contributors. Dusklight/
Aurora informed platform patterns; their game code is not linked. LLM-assisted
development with Claude Code/Codex and human hardware testing. Unofficial,
unaffiliated with SEGA/Nintendo.

Tag **v0.8.6-switch2-alpha.1** must point to the exact packaged source commit.
Publish ONLY this Switch 2 package/checksum as a GitHub pre-release, not latest;
leave stable v0.8.6 and all other platform assets/updaters unchanged. Alpha
installer updates are manual. Exact commit, ZIP hash and final cold GUI/CLI/
space results are supplied in the candidate record before approval.

Local easy-read guides are updated; existing public Claude artifact URLs need
external access/user assistance to republish. Draft only: no remote push/tag,
upload, release or announcement until final approval.
