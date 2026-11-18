# Switch 2 Alpha 1 development handoff

This is a documentation checkpoint, not a request to restart debugging. Use
[the alpha install guide](../SWITCH2-ALPHA.md) for player instructions and the
[published source tag](https://github.com/teclilla18/sonic-riders-recomp/tree/v0.8.6-switch2-alpha.1)
for native implementation files absent from main.

## Ownership and source separation

- Regular public development: the `sr-main` worktree on `main`. Preserve other
  sessions' uncommitted work before bringing in remote documentation commits.
- Switch 2 released source: tag `v0.8.6-switch2-alpha.1`, exact commit
  `1df03e0a90e96f7f0ac3e65ff6989b059c02a5d9`; local branch
  `codex/switch2-alpha1-final`. Create a new isolated worktree from this tag for
  future alpha fixes; do not merge experimental runtime into main implicitly.
- Local native experiment: `feature/switch2-native` at `b381e54` preserves the
  earlier higher-FPS experiment. Personal work on `codex/personal-switch2-unlimited`
  is separate, not released or hardware-accepted. Never publish private DX work.
- Machine paths, current checkout ownership, FTP connection, exact log/build
  locations and historical checkpoints belong in excluded `AI_HANDOFF.md`.

## Native architecture and reproducible build

The shared statically recompiled PowerPC core runs with native Switch 2 platform
services. Guest aliases use the hardware-tested stack-SVC backing map; this core
executes compiled host text, so guest RAM need not be executable. MMIO remains
handled by guest devices. The native Vulkan backend uses OunceVK and native
window/presentation services, with shared GX shader/rendering infrastructure.
HID input feeds the guest GameCube PAD path; native audio output transports the
existing guest audio mix. Native worker/TLS/core discovery and SDK timing were
tested on hardware. Dependencies include ounce-devkit, libounce, OunceVK,
devkitA64/switch-tools, shaderc/glslc and Dear ImGui. Exact pins and hashes are in
the tagged `setup/toolchain.json` and `diagnostics/switch2/dependencies.json`.

In a native-runtime checkout with validated own-disc inputs and pinned tools:

```text
python configure.py --target switch2 --devkitpro <devkitpro> --ounce-sdk <sdk> --glslc <glslc> --imgui <imgui>
ninja -j4
python tools/test_switch2_condition_wait.py
```

Use native Windows Python/Ninja. For users, the released installer is the supported
entry point; the CLI is documented in the install guide. Build output is under
`build/switch2/`. Keep generated code, builds and disc inputs local. Deployment:
quit first, retrieve logs before overwrite, back up the installed NRO/manifest,
transfer only matching NRO/build-info, and verify downloaded readbacks/hashes.
Keep ISO, saves, settings, caches and `env.txt`; retain `SR_SWITCH2_FRAMES=0`.
Default 2x/60 FPS, live resolution changes disabled. Never copy an entire test
directory over user data. Do not equate successful compilation with hardware acceptance.

## Confirmed fixes and their limits

**Scheduler:** snapshots showed a selected guest's host worker still parked in a
condition-variable wait while VI/HID continued, the run queue stopped progressing
and a guest IRQ remained queued. The native indefinite-wait wrapper swallowed
50 ms timeout returns, preventing the caller from rechecking its ready predicate
after a signal/timeout race. The targeted fix returns a permitted spurious wake
on that internal timeout, preserving explicit deadlines, errors and driver bypass.
`tools/test_switch2_condition_wait.py` extracts the actual function: deterministic
predicate/timeout injection fails the old implementation and passes the fix;
deadline/error/bypass cases pass. Source checkpoint `5db73f5`, build
`4a6ead9bbb02`, is the six-race rollback point.

**Logging:** measured event-pump stalls matched synchronous stdio write/flush
latencies (including about 4.16 seconds), while pad/overlay/applet portions did
not explain them. Native logging now enqueues into a bounded 256 x 1024-byte FIFO;
routine producers do not wait for SD I/O. A separate writer drains the queue;
checkpoint/fatal/shutdown barriers explicitly flush, and shutdown joins the writer.
FIFO/overflow/wrap/truncation/sequence tests passed, alongside scheduler and
platform-compatibility checks. Checkpoint `fa2ec57`, build `c8c06fdda996`.
This fixes a demonstrated blocking path, not every stutter or audio defect.

**Hardware evidence:** the user reported 13 consecutive races without a
character-selection freeze across scheduler-fix sessions, including the latest two
races on c8c06fdda996 without that freeze. This is promising regression
evidence, not proof of permanent elimination. Resolution was 2x; clocks were not
recorded, so do not invent performance comparisons. Ordinary gameplay and clean
overlay quit/return to Homebrew Menu were tested.

## Unresolved issues and deferred experiments

- Music and announcer can drop out while effects continue; announcer sometimes
  returns next race, music has required relaunch. Guest stream/voice/DVD/ARAM,
  scheduler and transport diagnostics have not established the permanent-loss cause.
- Temporary freezes, distortion and occasional heavy stuttering remain. Character
  selection clearing a sustained stutter was observed, not a proven causal fix.
- Synchronous Vulkan pipeline-cache saving explains some startup hitches. It does
  not explain every later hitch; no additional cache optimization is authorized.
- Live resolution changes could leave a frozen overlay with gameplay underneath;
  controls are disabled in this alpha. Extensive renderer changes were deferred.
- Earlier FPS build `67341d614683` froze at Player Entry. Interpolation was excluded
  from Alpha 1. Later scheduler fixes do not establish higher-FPS stability. Preserve
  `b381e54`, accepted `f05fd81`/`7d1bc5fcd419`, and validated `64e8316` rollback points.

## Implementation inventory and source map

Read this summary before the older native `docs/notes/switch2.md`: that historical
note still mentions 9000 frames and the original experimental branch. Current
Alpha 1 has `SR_SWITCH2_FRAMES=0` and higher FPS excluded.

| Stage | Implemented and verified scope | Evidence / limit |
|---|---|---|
| Standalone SR2Diag | Separate staged startup, SD/RomFS/services, counter, worker/TLS/core, HID, audible tone, mapping and Vulkan diagnostic NRO; persistent flushed logs and clean shutdown | Build `48b77e581423`: hardware passed all selected tests, RGB output personally confirmed; process-code alternative skipped automatically, stack map passed |
| Core integration | Isolated native configure/build target, shared recompilation core and guest devices, native mapping, Vulkan, controllers, audio and shutdown | Initial `12580193cc87` booted/demo raced; input and audio required follow-up |
| Accepted platform | Merged handheld/controller input, button-to-PAD/SI delivery, audio arithmetic/transport diagnostics, overlay/enhancements and persistent lifecycle logs | `7d1bc5fcd419` / `f05fd81`: user accepted tested gameplay and settings; later stress bugs were still possible |
| Alpha safety | 2x default, no automatic exit, live resolution controls disabled after overlay lockup | `80f7b92ba6c3` / `64e8316`; resolution defect trigger restricted, underlying recreation cause not established |
| Stall diagnostics | Independent focused stall snapshots, guest/host wait ownership, scheduler handoff, DVD/IRQ, voice/stream and pump/log timing breadcrumbs | `20e0f1f3fc7c`, `5c449024047a`, `67582dd7f046`, `3e551c8bf7f3`; diagnostics are observations and can perturb timing |
| Confirmed targeted fixes | Native condition-wait predicate recheck, then bounded asynchronous logging | `4a6ead9bbb02` / `5db73f5`, then `c8c06fdda996` / `fa2ec57` |
| Distribution | Dedicated Windows GUI/CLI Switch 2 setup, pinned dependencies, local build/SD install, package/audit/Sandbox flavors, manual alpha updates | Published `1df03e0`; no compiled game distributed; other platform installers unchanged |

Native source locations (in the tagged/runtime checkout, not all present on main):

- `runtime/src/platform/switch2/platform_switch2.c`: native initialization, HID,
  host worker condition waits, services, timing and shutdown.
- `runtime/src/platform/switch2/audio_switch2.c`: native output transport;
  shared `runtime/src/ax.c` owns guest mixing/voice diagnostics.
- `runtime/src/platform/switch2/log_switch2.c` and `log_queue.h`: logger writer,
  queue and flush barriers; shared `runtime/src/log.c` routes native output.
- `runtime/src/platform/android/render_vk.c`: shared Vulkan backend with native
  platform paths. Shared `sched.c`, `di.c`, `os_irq.c`, memory and input/device
  code provide guest scheduling, DVD completions, IRQ and PAD delivery.
- `tools/switch2_build.py`, `tools/switch2_install.py`, `configure.py`,
  `setup/setup.ps1`, `setup/toolchain.json`: native build/install graph and locks.
- `tools/make_release.py --flavor switch2`, `tools/audit_switch2_release.py`,
  `tools/sandbox_test.py`: established distribution workflow. Do not invoke a
  rebuild or Sandbox simply to resume reading; final results already exist.

Input root cause and evidence: a connected No1 entry was not sufficient to select
the working handheld stream. The accepted reader merges handheld input even with
No1 connected; raw A maps to GameCube `0x0100` and reaches guest SI INBUF. Preserve
that behavior instead of replacing it with assumptions about controller IDs.
Accepted-run audio transport reported 48 kHz stereo PCM16, no output drops or
delivery errors in 85 summaries; append/release/queued counts balanced. This did
not prove guest music/announcer health or uniquely attribute improvement to the
audio arithmetic correction. Don't use transport success to dismiss later dropout.

Dependency baseline: ounce-devkit `20261117`, SDK commit
`b11c29cba8d476f193bdc78ac98f344c167e541c`, libounce
`39406be1f25a2f1da3f7dfdaf7a42d2216cda4d0`, OunceVK
`114e9298fc0e6ba351d68a1b22be7f4072a759f8`. Verify the tagged lock rather than
upgrading tools. Dusklight `v2.0.3-switch2-1` and Aurora were references for native
patterns; their game code is not linked. Licenses/notices are retained in the tag.

Regression entry points already exercised: `tools/test_switch2_condition_wait.py`,
`tools/test_switch2_log_queue.c` and `tools/test_switch2_release.py`. Existing
native manifest/link/source-freshness checks and original-Switch preprocessing /
Android syntax checks passed during implementation. The personal FPS branch also
has render-rate/interpolation/ownership tests; these are not Alpha 1 acceptance.

## Hardware chronology and diagnostic interpretation

The six-race session on 4a6ead9bbb02 included regular Sonic/Blue Star and Super
Sonic without selection freezes. Music disappeared in race 1; announcer was absent
later. Stutters varied between races and were not consistently caused by pausing.
Three races on 67582dd7f046 had no selection freeze: one major hitch preceded loss
of music and announcer. Two races on 3e551c8bf7f3 had no selection freeze: results/menu
stuttering cleared after character selection; announcer recovered next race then
disappeared independently of stuttering. Two further races had no selection freeze,
temporary stalls/distortion in the first and an otherwise clean second. These
reported sessions are the 13-race regression evidence across sessions, not a claim
of one uninterrupted 13-race run. Keep the later c8 observations separate.

Startup evidence on the accepted early build: a warm cache prewarmed 44/44 pipelines
in about 1 ms, yet frame 240 spent 6736.22 ms saving cache with zero new pipelines
and about 0.03 ms presentation. Other saves were 103–578 ms; new pipelines could
still cost 214–260 ms. The save timer includes serialization and file writes, so
their separate costs remain unresolved. Prewarm-created pipelines mark the cache
dirty and schedule a save. Preserve caches; do not assume cache deletion is a fix.

## Resume without repeating work

1. Read the excluded canonical handoff for exact current paths, deployed build and
   log evidence index. Check branch/status before choosing a task.
2. Keep released Alpha 1, private personal FPS and unrelated main work separate.
   No unpublished runtime branch is implicitly authorized for pushing.
3. First pending external step: republish the two HTML guides and verify the live
   Switch 2 tabs. Personal Unlimited awaits a hardware report, not another rebuild.
4. If the user requests public native debugging, use a new isolated branch from
   the published alpha tag; first compare preserved focused reviews, then collect
   only missing evidence. Audio cause remains unresolved; cache work is lower priority.
5. Do not replace/repackage the accepted release merely to update these notes.
   A future runtime candidate needs physical acceptance before release regeneration
   and final cold validation, following the existing workflow and approval gates.

## Published release and validation

Alpha tag/source above; runtime c8c06fdda996, scheduler plus asynchronous logger.
Only `SonicRiders-Switch2-Setup-Alpha1.zip` and its checksum are published; the
pre-release is not latest stable (v0.8.6 remains stable). ZIP: 7,733,960 bytes,
SHA-256 `0f34133f0c9c6dff68dff6f6ad4a9ba7a79cffe69c1b1dd8be4390a72f63c495`.
Whole-distribution audit passed: clean source match, game-data checks, dependency
pins/hashes and private-path checks. Final cold Windows Sandbox GUI and CLI/space
tests passed using the exact ZIP, fresh dependency downloads, MOTW/Explorer
extraction, read-only own disc, results-only writable mapping and minimized launch.
Both setup exits were zero; native platform ID, manifest source, SD layout and
user-data preservation sentinels passed. Sandbox proves build/install, not hardware.

## Public documentation and remaining handoff

Main announcement `d8d12f7` added the exact user announcement, unchanged full-width
Lanmana banner/signature, approved artwork SHA-1, credit, alpha links, separate
platform references and bug form. Repository description includes Switch 2 alpha.
Both interactive HTML guides have dedicated Switch 2 tabs; six-tab navigation and
click/keyboard checklists passed in Edge, with original platform panels preserved.
The existing external Claude Artifacts still need republication and live verification.
This session restyles only the existing alpha release body, retaining its entire
old technical description in a collapsed section; tag/assets/stable release stay intact.

Prior public priorities remain: background texture-pack loading on original Switch,
off-by-default auto stick-spin option (#23), Thursday iOS package, then paused online
lobby work. See STATE.md for other sessions' ownership; don't silently replace these
priorities with Switch 2 debugging. No further features, fixes or builds this session.
