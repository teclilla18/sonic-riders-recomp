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
| [notes/architecture.md](notes/architecture.md) | Reference | project goal, GLSL (uam) rules, device/HLE boundary, shader cache plan, architecture decisions, SDK map, naming |
| [ONLINE.md](ONLINE.md), [PERF.md](PERF.md), [MODDING.md](MODDING.md), [ANDROID_PLAN.md](ANDROID_PLAN.md) | Reference | online play, perf test, mods, Android plan |
| [notes/ps4-ps5-research.md](notes/ps4-ps5-research.md) | Investigation | PS4/PS5 homebrew scene (2026-11-03): jailbreak firmware, PS5 RADV Vulkan driver, recomp ports, suggested order |
| [archive/recon-2026-10.md](archive/recon-2026-10.md) | Historical | first recon, the superseded matching-decomp plan, original debugging and Eden notes, recon-era open questions |
| [archive/history-2026-10-16_to_25.md](archive/history-2026-10-16_to_25.md) | Historical | HISTORY entries 2026-10-16 to 2026-10-25 |

Kinds: **Reference** = maintained, current; **Historical** = true when written, "current" / "next" in it meant then;
**Investigation** = analysis of a problem and its evidence limits; **Draft** = design or research, not evidence of
implementation. When a note is superseded, move it to `docs/archive/` with `git mv` and fix the links here.
