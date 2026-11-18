# State (2026-11-18)

Read this first, then canonical excluded AI_HANDOFF.md for ownership/deployments.
This checkpoint is on `codex/issue32-vulkan-legacy`, not main. Preserve other sessions'
dirty sr-main work and other branches. Keep this experiment isolated from main.
References: docs/REFERENCE.md; code: docs/MAP.md; evidence: docs/HISTORY.md.

## Issue #32: experimental Vulkan 1.1 compatibility

- **Implemented:** automatic traditional render-pass/framebuffer fallback when queried dynamic
  rendering or its dependencies are missing. Existing supported-device dynamic path retained.
  Shared Android/iOS renderer; centralized pipeline/pass changes and fence/view/resize/recovery cleanup.
- **Implemented:** depth transfer usage, image/format/limit checks, capped image pool with dedicated
  spills, prewarm-safe compatible passes and separate path/feature caches.
- **Implemented:** single-source shaders and separate alpha writes preserve Claude's solid-floor
  TEV/stored-alpha fix (public 0c00ba4). Normal dual-source behaviour remains unchanged.
- **Tested [confirmed]:** Windows/ARM64 source-extracted helper regressions, Vulkan 1.1 SPIR-V
  validation, Switch shader generation/zero scratch. S23 Ultra / Adreno 740 / Vulkan 1.3.128: full
  1P/2P effects routes, dynamic 50.283/56.734 fps, legacy 49.650/56.240 fps (1,800 measured steps
  each). Sequential runs do not establish a speed regression. Captures closely match/solid floors.
- **Tested [confirmed]:** Khronos validation clean through legacy/no-dual/no-push racing and
  simulated loss, plus dynamic recovery. Legacy background/resume continues rendering.
- **Tested [confirmed, reporter 2026-11-18]:** build 3b2592b7 automatically selects legacy on
  Mali-G57 MC3 / Vulkan 1.1.177. Reporter completed a race without issues; log reaches 17,700
  presented frames, 100% shader coverage. Auto maintains ~60 steps/s even at 30.6 rendered fps.
- **Implemented:** ordered Android quit: park producers, join pipeline/UI workers, close audio,
  wait GPU, destroy Vulkan/SDL, run owned save/network/record handlers and flush before `_Exit`.
- **Tested [confirmed]:** S23 dynamic-path shutdown reproduced a libc finalizer race (FORTIFY,
  destroyed mutex in Android/vendor RenderThread). Both paths pass delayed clean menu exits;
  Full Khronos race/natural quits, simulated recovery/menu resume and 5-second holds pass both
  paths without validation errors/fatals. Normal signed APK exits pass without delay.
  Windows/ARM64 concurrency/exit-handler tests and x86_64 Android syntax pass. Details in the note.
- **Uncertain:** exact original Mali SIGSEGV attribution; reporter must retest updated package.
  Not released; real iOS/MoltenVK, real GPU reset, exhaustive alpha/depth overlaps,
  all stages and long-session memory pressure remain unproven.
- Prepare standard experimental Android source/setup ZIP only; no generated APK sharing, public
  release, main merge, issue reply or closure authorized. Details: docs/notes/vulkan-legacy.md.
- Separate pacing audit **tested [confirmed]**: matched-capture runs used SR_DETCLOCK, which
  intentionally bypasses adaptive pacing. Normal Auto at 2x/effects keeps 59.94-59.95 steps/s:
  dynamic 44.56/52.64 fps, legacy 42.93/51.76 fps (1P/2P). GPU waits reduce rendering, while
  simulation remains full speed. No normal Auto regression reproduced; no pacing code changed.

## Switch 2 continuity (preserved)

- **Released:** separate native prerelease v0.8.6-switch2-alpha.1, source 1df03e0, runtime
  c8c06fdda996. Default 2x/60; live resolution controls disabled. Stable release untouched.
- Scheduler wait and async logger fixes have hardware regression evidence; permanent freeze
  elimination and music/announcer-loss causes remain unproven. No native edits in this task.
- Personal Unlimited experiment stays private, isolated and not hardware-accepted. Preserve
  all rollback trees, matching manifests, raw logs and console backups.
- docs/notes/switch2-handoff.md has architecture, fixes/tests, dependency pins and resume gates.
- Announcement, unchanged credited banner and interactive guides verified. External Claude Artifact
  install/update guides still need republication and live Switch 2 tab verification.

## Prior user priorities (preserved; no change of order implied)

1. Ready-made Android APK in a SEPARATE public repo: rule/guard exception first; disc picker
   (android-disc-picker, 2394600, tested S23), optional RVZ converter, then repo/release/link.
   No APK distribution from this repo. Advertise separate repo at every release once it exists.
2. Thursday 2026-11-19: Switch texture-pack background loading, off-by-default auto stick-spin
   (#23, promised next release), iOS ZIP from the Mac/tag (last iOS v0.8.4).
3. Hold-B continuous boost: repeat-boost air drain; High Booster retains one-press behaviour.
4. #29 split-screen Lv. 4/5 HUD still displays Lv. 3; cosmetic, known in v0.8.5/v0.8.6.
5. #23 Ayn Thor slowdown: GPU-bound busy frames suspected; compare GPU/blur/frame-rate modes.
6. #31 updater awaits reporter version. #30 closed after Steam Deck reporter confirmed setup.
   #18 WGP freeze, #12, #24, music (#17), right-stick camera and boost camera (#16) remain open.
7. Roster expansion approved design (docs/notes/roster.md), paused.

## Stable release / texture packs

- **Released:** v0.8.6 (2026-11-16, e19f0dd; PC/Switch/Android/Linux; cold Sandbox PC+Android PASS).
  HD packs support DDS BC1/2/3/7, Dolphin names, mods and Switch 512 cap.
- PC pack **accepted**; tested 1 GB / 1,074 textures at 59.9 fps with no slow demo frames.
- Switch pack works but synchronous loading freezes (65 hitches, 46-114 ms typical, worst 1.3 s).
  Plan: low-priority read/decode worker, original image on miss, safe replacement retirement.
  Installer SD copy not tested end to end. Android/iOS packs not device-validated.
- Linux #30 setup fix committed at 1235bc4; reporter confirmed Steam Deck build/launch, issue closed.
- Keep 5 newest releases plus v0.8.2 forever. Update easy-read guides at actual releases;
  iOS signatures expire 2026-11-21. No release publishing from this experimental task.

## Online (paused for Thursday work)

- **Confirmed:** PC-Switch and Switch-Switch 2P, 15,000 matching checksums. Per-player screen
  shapes (protocol 4) tested both directions, not accepted by user play.
- Game 30 fps mode is shared state. Next approved: agreed-frame room-wide 30 fps switch,
  host rules, stat mods off, server (docs/notes/online-research.md step 4).
- Lobby design approved, not implemented: room codes/list, host rules, punch/relay, no accounts,
  Discord, Online overlay; joining restarts/replays input history for catch-up.

## Next checks

- Reporter retest of updated shutdown package; require completed cleanup and no fatal signal.
  Original Mali fault PC is unmapped; do not claim that exact crash fixed before confirmation.
  Keep #32 open and branch unmerged for user decision.
- Real Mac/iOS/MoltenVK build/test before integrating/releasing shared renderer changes.
- Any further normal-play slowdown needs logs with SR_DETCLOCK off and the selected mode stated.
- Switch pack worker: three stages/full pack, memory/hitches; installer SD copy end to end.
- Room-wide 30 fps switch on two consoles; stock-clock swapchain/performance comparison.
- Device identities/clocks/deployments and local test paths stay in excluded AI_HANDOFF.md.
