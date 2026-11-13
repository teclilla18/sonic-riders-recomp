# Reference (index)

Durable facts about the port live in topic notes; this page only points to them. Dated stories of how things were
found: [HISTORY.md](HISTORY.md) (older entries in [archive/](archive/)); current status: [../STATE.md](../STATE.md);
where code lives: [MAP.md](MAP.md). Open only the note you need, and grep inside it (`grep -n '^## '`).

| Note | Kind | What is in it |
|---|---|---|
| [notes/build-and-test.md](notes/build-and-test.md) | Reference | **Quick commands** (configure/build/run for every target, phone runs, RAM tools), Switch / PC builds, setup, releases, GitHub, Eden / Dolphin / test routes |
| [notes/android.md](notes/android.md) | Reference | Android build, install, test, touch controls, app icon, triggers, phone facts |
| [notes/debug-variables.md](notes/debug-variables.md) | Reference | every `SR_*` debug variable, `RECOMP_*` switches |
| [notes/game-addresses.md](notes/game-addresses.md) | Reference | game addresses (modes, players, gear, SDK map pointers) |
| [notes/gear-stats.md](notes/gear-stats.md) | Reference | vanilla top speed / boost per gear and level (GameFAQs guide numbers) |
| [notes/architecture.md](notes/architecture.md) | Reference | project goal, GLSL (uam) rules, device/HLE boundary, shader cache plan, architecture decisions, SDK map, naming |
| [ONLINE.md](ONLINE.md), [PERF.md](PERF.md), [MODDING.md](MODDING.md), [ANDROID_PLAN.md](ANDROID_PLAN.md) | Reference | online play, perf test, mods, Android plan |
| [notes/ultrawide-fov.md](notes/ultrawide-fov.md) | Plan | ultrawide without stretching (fit-window mode, wider EFB, 2D at 4:3 proportions) and a FOV setting; FOV input of `fn_800645DC` |
| [notes/picture-options-port.md](notes/picture-options-port.md) | Draft | PC picture options on Linux / Android / Switch: what exists, agreed order, Android (Vulkan) pass design |
| [notes/reference-projects.md](notes/reference-projects.md) | Reference | other recompilation projects we borrow ideas from: what each is, what was taken, license cautions |
| [notes/launcher.md](notes/launcher.md) | Reference | Windows launcher: build, pages, settings, install flow, SR_LAUNCHER_SHOT |
| [notes/plans-2026-11.md](notes/plans-2026-11.md) | Plan | look-back camera, nitrous-style boost toggle, windowed launcher, building on Linux/Deck/Android, custom icons per device |
| [notes/boost-effect.md](notes/boost-effect.md) | Plan | NFS-style boost camera (FOV kick, pull-back, edge radial blur) at boost levels 2/3; research on speed cues; steps |
| [notes/ps4-ps5-research.md](notes/ps4-ps5-research.md) | Investigation | PS4/PS5 homebrew scene (2026-11-03): jailbreak firmware, PS5 RADV Vulkan driver, recomp ports, suggested order |
| [notes/ios.md](notes/ios.md) | Current | iOS port (first boot 2026-11-07; build/test commands at the end): SDL2 host layer + Vulkan renderer on MoltenVK, `<threads.h>` shim, free-account signing (7 days), Documents folder for disc/saves/log, touch + pads, steps |
| [notes/vita-research.md](notes/vita-research.md) | Investigation | PS Vita scene (2026-11-03): HENlo/Enso, VitaSDK, vitaGL/GXM, Vita recomp ports, CPU estimate, remote control (vitacompanion), probe build |
| [archive/recon-2026-10.md](archive/recon-2026-10.md) | Historical | first recon, the superseded matching-decomp plan, original debugging and Eden notes, recon-era open questions |
| [archive/history-2026-10-16_to_25.md](archive/history-2026-10-16_to_25.md) | Historical | HISTORY entries 2026-10-16 to 2026-10-25 |

Kinds: **Reference** = maintained, current; **Historical** = true when written, "current" / "next" in it meant then;
**Investigation** = analysis of a problem and its evidence limits; **Draft** = design or research, not evidence of
implementation. When a note is superseded, move it to `docs/archive/` with `git mv` and fix the links here.
