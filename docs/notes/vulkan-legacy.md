# Vulkan 1.1 rendering compatibility (#32)

Implemented on the experimental issue branch, not released or accepted on Mali.
The shared Android/iOS renderer selects dynamic rendering only when the negotiated
instance/device API, extension dependencies and queried `dynamicRendering` feature
allow it. On Vulkan 1.1 the extension path requires both
`VK_KHR_depth_stencil_resolve` and `VK_KHR_create_renderpass2`; Vulkan 1.2 supplies
those dependencies. MoltenVK's portability-subset extension remains enabled when
advertised. No OpenGL fallback or separate renderer was introduced.

## Rendering and object lifetime

- `pipe_build` uses either dynamic attachment formats or a compatible render pass.
- `efb_begin`/`efb_end`, `target_begin` and `rendering_end` cover all game, copy,
  masked-clear, bloom/blur, grading, FSR, presentation, overlay and touch passes.
- Legacy render passes retain colour/depth LOAD/STORE, clear black with alpha one
  only where requested, and retain the existing render areas and explicit layouts.
  External dependencies order attachment reads/writes between passes; internal
  stage masks use graphics attachment stages, not `ALL_COMMANDS`.
- Immutable passes are cached by colour/depth format and load operation under a
  mutex, including background pipeline compilation. LOAD/CLEAR variants are
  compatible. Passes outlive their pipelines and framebuffers.
- Framebuffers are cached by views and extent. View destruction removes associated
  framebuffers at texture fence retirement, post-image resize, swapchain recreation
  or idle device teardown. Device recovery recreates every object.
- A swapchain format change stops prewarm and invalidates incompatible pipelines
  after GPU idle. Opaque caches are separate for legacy/dynamic and single/dual
  source; single-source state keys have a separate versioned file. Ordinary caches
  and state-key files remain usable on the existing supported-device path.

## Other compatibility checks

Depth images now include TRANSFER_DST usage for the startup clear. Depth selection
checks the actual image usage/extent; RGBA8 checks rendering, blending, sampling,
linear filtering and blitting. Image creation checks usage/extent support; shader
bindings are checked against hardware limits. The 384 MiB pool is capped by the
maintenance3 allocation limit and heap size, retries smaller allocations on OOM,
and spills full/fragmented/incompatible images into separately retired allocations.
This avoids requiring one 384 MiB allocation; it cannot guarantee sufficient RAM.

The shaders use SPIR-V 1.3 targeting Vulkan 1.1 and only Shader/ImageQuery
capabilities. Optional dual-source blending needs special handling: the historical
floor fix (public commit `0c00ba4`) blends with TEV alpha while storing BP42's
constant alpha. Single-source colour shaders therefore retain TEV alpha; a separate
alpha-write pass stores the constant with the original alpha blend operation.
The depth-writing repeat uses EQUAL without another depth write. The usual
dual-source path is unchanged. Captured Metal City views match closely, but more
unusual overlapping geometry/alpha/depth combinations remain a validation item;
do not claim universal pixel equivalence from this route.

Diagnostics (not persisted in settings):

- `SR_VK_FORCE_LEGACY=1`: force traditional render passes; startup logs the path.
- `SR_VK_NODUALSRC=1`: select single-source shaders/separate alpha writes.
- `SR_VK_NOPUSH=1`: exercise the existing descriptor-pool fallback.
- `SR_VK_FAKELOST=<frame>`: existing simulated device-loss recovery test.

## Validation (2026-11-18)

- **Tested [confirmed]:** Android ARM64 APK builds; source-extracted C regressions
  run on Windows and ARM64 Android, covering feature/dependency combinations,
  concurrent pass-cache callers, compatible pipelines/passes, render areas,
  LOAD/CLEAR/STORE, framebuffer retirement/recreation, alpha separation and pool spill.
- **Tested [confirmed]:** generated SPIR-V passes `spirv-val --target-env vulkan1.1`;
  single-source variants have no secondary blend-output index. Switch shader
  generation and zero-scratch checks pass (default shader behaviour retained).
- **Tested [confirmed]:** S23 Ultra, Adreno 740, Vulkan 1.3.128, Android. Full
  deterministic 1P/2P attract-demo routes with 1x, bloom 25%, non-neutral grading
  and FSR sharpness 50: dynamic **50.283 / 56.734 fps**, forced legacy
  **49.650 / 56.240 fps**, 1,800 measured steps per scene. Simulation speed equalled
  FPS in these runs because `SR_DETCLOCK=1` intentionally bypasses adaptive pacing.
  These are matched-capture diagnostics, not normal Auto benchmarks. Single sequential
  runs do not establish a performance change.
- **Tested [confirmed]:** three matching dual-source captures look equivalent;
  fewer than 0.02% of colour channels differ, including the live FPS overlay.
  Forced single-source captures also show solid floors and matching scene/timer;
  they are close, not pixel-identical.
- **Tested [confirmed]:** Khronos validation 1.3.296 on the S23, legacy plus
  no-dual/no-push through racing and simulated loss at frame 4,201; dynamic recovery
  likewise reaches the post-recovery capture. Corrected runs have no validation
  warnings/errors/VUIDs. Legacy background/resume continues rendering.
- **Reporter-tested [confirmed, 2026-11-18]:** source build `3b2592b7` on Mali-G57 MC3,
  Vulkan 1.1.177, driver `08001000`: automatic legacy selection with dynamic feature/
  extension absent, separate alpha pass and descriptor pool. Reporter confirms a full
  race without issues in [the follow-up](https://github.com/teclilla18/sonic-riders-recomp/issues/32#issuecomment-6988716603).
  Log reaches 17,700 presented frames; all 59 shader-coverage samples are 100%.
  Race samples draw 30.6-58.5 fps; Auto usually keeps 59.8-60 steps/s, including
  60 steps/s at 30.6 fps. Cold compilation causes hitches (worst 1,077 ms frame,
  1,059 ms compiling 18 pipelines) and temporary simulation slowdowns.
- **Quit failure [confirmed]:** `[android] quit` immediately precedes SIGSEGV in
  the returned Mali log, despite successful gameplay. No Vulkan error/device-loss
  message precedes it. Existing `render_thread` calls `exit(0)` there; this path
  was not edited by the original #32 fallback. The crash handler's stack does not
  establish the exact Mali fault's cause. The shutdown follow-up below is an experimental fix.
- **Unproven:** real iOS/MoltenVK execution (no Mac SDK/device used); real GPU reset; exhaustive
  alpha/depth overlap cases, all stages and long-session memory pressure.

Separate frame-pacing audit (no pacing source changes): normal Auto (`SR_FRAMERATE=1`,
**no** `SR_DETCLOCK`), 2x/bloom 25%/grading/FSR 50 on the same S23:

| Path | 1P simulation / rendering | 2P simulation / rendering |
|---|---|---|
| Dynamic | 59.95 steps/s / 44.56 fps | 59.94 steps/s / 52.64 fps |
| Legacy | 59.94 steps/s / 42.93 fps | 59.95 steps/s / 51.76 fps |

Both complete 1,800 measured steps per scene. Normal Auto maintains full speed while
skipping drawing; GPU frames around 25-26 ms and render waits around 16-20 ms explain
the lower display rate. No normal Auto regression was reproduced on either path.
Raw values remain in local perftest JSON. Single sequential runs are not a controlled
speed comparison.
The host glue returns -1 for `platform_gpu_frame_ms` on Android, so `[pace] GPU -1`
means unavailable: use the Vulkan renderer's `[vk] GPU` timestamps. Auto schedules
from retrace lag; the missing hint is a separate 60/30 hysteresis audit item, not a
demonstrated cause of this observation.

Reproduce focused checks (no disc needed):

```text
py -3 tools/test_vk_legacy.py --cc <clang> --vulkan-include <Vulkan-header-root>
py -3 tools/test_vk_shaders.py --validator <spirv-val> --dksh build/switch
```

Reporter distribution must use the standard experimental Android **source/setup
ZIP**, built by `tools/make_release.py --flavor android` from a committed branch.
Never share the locally built APK, generated code, shaders, logs or disc files.
Mali boot/gameplay are reporter-confirmed; the shutdown follow-up still needs reporter verification.
Keep #32 open for the user's decision. No release,
merge into main or external reply is authorized by this experiment.


## Android shutdown follow-up (2026-11-18, experimental)

The reporter log gives an unmapped SIGSEGV PC after quit, insufficient to prove its
exact cause. The old `exit(0)` quit block predates #32. Unchanged S23 menu and full
race baselines passed both paths, so the original Mali crash was not reproduced.

An intermediate explicit-teardown candidate still calling libc `exit` reproduced a
**dynamic-path** S23 crash: teardown completed, then 8 ms later FORTIFY reported a
destroyed pthread mutex in Android's RenderThread. The stack traverses libc++, QTI
gralloc/mapper, Adreno EGL and libhwui. Android also recorded EXIT_SELF/status 0;
that record alone is insufficient to validate quit. This establishes a shared
Android finalization hazard, not a legacy-only failure. It does not prove that the
reporter's Mali SIGSEGV has precisely the same cause.

Bionic [exit.cpp](https://android.googlesource.com/platform/bionic/+/741541d307f2b48c5c87fb7f38e6618d3175dbd0/libc/bionic/exit.cpp)
runs thread/process finalizers before terminating the process. Android framework
and vendor threads can still use library globals during those finalizers.

Implemented shutdown order:

1. Gate frame producers under the existing render lock; leave guest workers parked.
2. Cancel remaining prewarm jobs, finish any active driver call, join the SDL thread
   (including TLS/JNI cleanup), then drain completed pipelines. Recovery and format
   invalidation use the same join. Preserve the original 8 MiB worker stacks.
3. Stop/join the overlay raster worker before releasing its ImGui lists/context.
   Serialize audio close with queue/open/pause; later producer calls return safely.
4. Save the cache; wait GPU idle; destroy pipelines and legacy framebuffers/passes
   before image views/images, then remaining children/device, surface and instance.
   Reuse the existing device teardown, retirement and swapchain ownership rules.
5. Destroy the SDL window and quit SDL. Run application-owned card/network/record
   handlers in LIFO order, flush streams, then Android `_Exit` without process-wide
   library finalizers. Keep crash handling active throughout cleanup. Do not return
   SDL_main while detached guest workers can execute libmain (SDL unloads it).

`rt_atexit` retains the existing application handlers, including registration from
another handler and the fallback standard-exit path. Windows/iOS/other platforms
retain libc exit semantics. Shared iOS renderer cleanup uses the same worker join;
real MoltenVK execution remains untested. Screenshot writers retain only copied CPU
pixels/file state, no Vulkan/SDL handles. Early non-renderer diagnostic exits are
outside the normal interactive shutdown path and were not rewritten.

Focused Windows and ARM64 tests cover in-flight compile/audio quiescence,
cancellation/join/drain ordering, idempotence, no audio calls after SDL close,
handler LIFO/reentrancy/capacity, buffered writes, exit status and unchanged host
finalizer semantics. The existing feature/pass/framebuffer/pool regressions pass.

`SR_SHUTDOWN_DELAY_MS=1..5000` holds the process alive **after** completed cleanup
and application handlers, before termination. Both S23 menu paths survive a 500 ms
hold, well beyond the observed 8 ms failure window, and exit EXIT_SELF/status 0
without fatal signals. Full 1P/2P natural-quit routes also pass with Khronos validation loaded, zero
warnings/errors/VUIDs/fatal signals, and complete cleanup plus a 500 ms hold:

| S23 path | 1P steps/s / fps | 2P steps/s / fps |
|---|---|---|
| Dynamic | 59.95 / 51.09 | 59.95 / 56.38 |
| Legacy + no-dual + no-push | 59.83 / 26.59 | 59.97 / 55.27 |

Each scene measures 1,800 steps. Validation/forced compatibility settings and
sequential runs make these unsuitable as release performance comparisons. Normal
Auto remains enabled, deterministic timing disabled; no pacing code changes.

Additional Khronos runs on both paths recover from simulated loss at frame 31,
background/resume, recreate the swapchain, resume menu rendering at 59.9 fps and
exit cleanly after a 5-second post-cleanup hold. No validation warnings/errors or
fatal signals. This tests menu resume, not mid-race gameplay resume or real GPU
failure. Final normal signed-build menu exits also pass both paths with no hold;
validation APK removed and temporary GPU debug settings restored.

Reproduce (local device and own disc build required for app tests):

```text
py -3 tools/test_android_shutdown.py --cc <clang> --vulkan-include <Vulkan-header-root>
py -3 tools/test_runtime_exit.py --cc <clang>
py -3 -m unittest discover -s tools -p test_android_quit_test.py
py -3 tools/android_quit_test.py --serial <adb-serial> --out build/quit-dynamic --require-cleanup --args "SR_SHUTDOWN_DELAY_MS=500"
py -3 tools/android_quit_test.py --serial <adb-serial> --out build/quit-legacy --trigger perftest --require-cleanup --args "SR_VK_FORCE_LEGACY=1 SR_VK_NODUALSRC=1 SR_VK_NOPUSH=1 SR_SHUTDOWN_DELAY_MS=500"
```

The app test waits for the actual tested PID to exit; force-stop is setup only.
It checks fresh logcat/fatal signals plus the matching ApplicationExitInfo and both
cleanup markers. Optional `--background-resume` exercises Home/resume before menu
quit. A previous-run crash dialog is preserved in logs and dismissed before taps.
No release, merge, issue closure or external reply is authorized.


## Reporter shutdown verification and Android asset update (2026-11-18)

The [Splash Canyon/Amy follow-up](https://github.com/teclilla18/sonic-riders-recomp/issues/32#issuecomment-6991852326)
and [Green Cave/Tails follow-up](https://github.com/teclilla18/sonic-riders-recomp/issues/32#issuecomment-6992322743)
both use source 90868613 on Mali-G57 MC3 / Vulkan 1.1.177 with automatic legacy,
separate alpha and descriptor-pool fallback. **Reporter-tested [confirmed]:** both
logs complete all cleanup/application-handler phases, terminating status 0, without
another signal or post-quit fault. This verifies the updated shutdown behaviour on
the reporter's device; it does not map the original faulting vendor PC.

The Green Cave post-race loading hang keeps rendering near 60 fps with GPU around
7 ms and negligible render wait; no Vulkan error or device loss. It is a separate
loading-progress investigation, consistent with #6 but missing the watchdog state
needed for exact attribution. See [loading-hang.md](loading-hang.md) for provenance,
comparison with the old CRI/DI findings and next diagnostics. No speculative loader
fix is included in the Android update.

Current authorization supersedes the experimental-only publication limits above:
update only the existing v0.8.6 Android source/setup ZIP after cold setup checks.
The runtime remains the tested 90868613 implementation; this follow-up changes
documentation only. No new tag, main merge, other platform asset replacement,
issue closure or external issue reply is authorized. Real MoltenVK remains untested.


### Publication result

**Released, 2026-11-18:** replaced only
[`SonicRiders-Android-Setup-v0.8.6.zip`](https://github.com/teclilla18/sonic-riders-recomp/releases/download/v0.8.6/SonicRiders-Android-Setup-v0.8.6.zip)
on existing v0.8.6. Package source faab3dd4, runtime implementation 90868613,
249 entries, 8,210,435 bytes. SHA-256:
`fa1b71f0ef35bdeef810458d925a6c90fd6af39f7b66708fb38990846678d67e`.

User-approved cold Windows Sandbox tests pass for PC (90868613 implementation)
and the exact Android ZIP (faab3dd4, documentation-only follow-up), both standard
and space-containing install paths. Fresh downloads, mark-of-the-web extraction,
exit 0 and expected executable/APK existence confirmed. These are setup/build
checks; gameplay validation is the earlier S23 and reporter evidence above.

The published Android ZIP was downloaded and hash-verified. Original release notes
were preserved and an Android update/source/checksum/known-hang section appended.
The v0.8.6 tag, release identity and PC/Switch/Linux asset IDs/digests are unchanged.
No main merge, new tag, public APK, issue reply or closure. Original Android asset
and before/after publication audits remain backed up locally. Newer documentation
commits do not require repackaging this verified runtime/source artifact.
