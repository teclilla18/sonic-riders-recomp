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
  was not edited by #32. The crash handler's stack does not establish the fault's
  cause or exclude a driver/resource-lifetime interaction. Do not claim clean exit.
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
Mali boot/gameplay are now reporter-confirmed; the quit crash still needs investigation.
Keep #32 open for the user's decision. No release,
merge into main or external reply is authorized by this experiment.
