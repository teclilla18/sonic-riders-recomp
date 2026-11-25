# Standalone Android installer

Phase 1 feasibility, 2026-11-25. Source reviewed at `2f1d028`. The existing Android game runs on the S23 Ultra
(see [Android notes](android.md)). The milestone 1 APK is ready for physical device testing.

## What the Windows installer does today

| Step | Repository evidence and disc dependency |
| --- | --- |
| Verify and extract | `setup/setup.ps1` validates the clean GXEE8P rev 0 image by full-file SHA-1, converting RVZ/WIA to ISO first when needed. `tools/extract_iso.py` checks the disc header and extracts and verifies `main.dol`, `_Main.rel`, and `SonicR.str`. The third file is extracted by setup. The recompiler consumes the DOL and REL. |
| Analyze and translate | `tools/build_workdir.py:dtk` runs pinned decomp-toolkit (`dol split`) with `config/GXEE8P/config.yml` on the extracted executables. `tools/recomp/gcrecomp.py` reads both generated symbol lists, the DOL/REL and the tracked HLE/patch rules. It emits `code_*.c`, `funcs.h`, and `table.c` with the dispatch table, HLE metadata and entry point. These outputs stay local. |
| Compile and package | `configure.py` compiles generated C and runtime sources together into `libmain.so`. `tools/android_build.py` builds SDL2, compiles SDL and project Java to DEX, and packs the DEX, `libSDL2.so`, `libc++_shared.so` and `libmain.so` into a signed APK. Only the generated game part inherently requires the disc. Runtime sources currently have generated-header build dependencies, and the generated code calls runtime helpers and globals. |
| Install and play | `tools/make_release.py` distributes the Windows setup zip. `setup/setup.ps1` installs the locally signed APK and copies the verified ISO with ADB. At launch, `runtime/src/disc.c` opens the ISO. `loader.c` reads the DOL and links the REL into guest memory, and `di.c` serves disc reads during play. The retained ISO supplies game data even after compilation. |

## Recommended architecture

Ship one signed arm64 APK with a setup entry point and the existing SDL game activity. Prebuild the project runtime,
SDL2, ImGui, Java, project artwork and Vulkan shaders from distributable inputs. Use only the tracked shader keys
approved by the repository. Start from the existing disc-free `--android-app probe` path for packaging. Replace the
current monolithic `libmain.so` with a prebuilt SDL entry library and a locally compiled game module.

1. Pick a local full ISO with Android's Storage Access Framework. Stream validation and copy into an app-owned
   `baserom` staging file in one pass so the existing path-based disc reader can be reused. Commit the validated copy
   atomically. A copy also works with nonseekable providers and survives a moved source file. Evaluate persisted,
   seekable URI access later if the storage cost warrants changing the disc reader.
2. Embed a minimal Python runtime for the existing Python scripts. Cross-build the pinned Rust decomp-toolkit for
   Android, preferably exposing its analysis as a library call in the build worker. Cross-build an Android-hosted
   AArch64 Clang/LLVM and LLD subset, with only the needed headers and target link inputs. Use packaged native tools
   through library entry points, with no desktop SDK, Java tools, Ninja, or package signing on the phone. Give
   decomp-toolkit a writable staging tree and reuse its output locally across compatible updates. Verify licenses,
   redistribution terms, notices, and pinning for every bundled tool and header.
3. Compile the generated C and table into one PIC ELF module. Start with an audited set of runtime exports and a
   single versioned C module entry returning the tables and entry point. Decouple runtime declarations from
   `funcs.h`. Link-check imports against the packaged runtime ABI. Keep the current `PPCContext`, helper semantics,
   compiler flags (including FP contraction off and signed wrap), direct calls inside the generated module, and
   address-based dispatch in `cpu.c`. The phone builds native code ahead of play. SDL keeps ownership of input,
   audio, lifecycle and presentation.
4. Build in an app-private worker process with bounded compile parallelism. Use a private, read-only final module,
   bind it to the verified disc digest, ABI, runtime interface and translator/config/toolchain inputs, then atomically
   activate it after a loader smoke check. Reuse compatible artifacts on subsequent launches. Rebuild incompatible
   artifacts from the retained ISO after an APK update. Put temporary analysis, generated C and objects in a separate
   workspace from the ISO, saves, settings and mods. Handle cancellation, process death and failed rebuilds by
   cleaning staging and retaining the last complete artifacts. Load them only with a compatible runtime.

### Alternatives and constraints

- **Prebuilt runtime plus extraction alone** cannot supply the generated ARM64 functions and dispatch table.
  Prebuilding those from the disc would distribute game-derived code. A replacement translator that emits machine
  code directly would duplicate existing compiler work. Translating and compiling only the game module preserves the
  current backend and keeps repeated builds small in scope.
- **Full phone build** still needs Android-hosted versions of the tools and a way to run their output. It also adds
  SDL/ImGui/runtime and shader rebuilds, SDK packaging, signing, and a second package install. The NDK target triple
  selects Android output. The supported NDK host tools run on desktop hosts. Linux AArch64 decomp-toolkit is likewise
  a different artifact from an Android/Bionic build. [NDK host tools](https://developer.android.com/ndk/guides/other_build_systems)
- **Native loading is the gating risk.** Android blocks direct `execve` of files in writable app home for modern
  targets. Use the platform linker to test a locally produced PIC module in internal storage, with read-only final
  files, no text relocations and W^X segment permissions. Android 14's read-only dynamic-loading guidance illustrates
  DEX/JAR/APK. It gives no portability guarantee for a generated ELF, and an NDK loader API does not override SELinux.
  Test release-equivalent target SDK, linker namespace, relaunch and ELF/ZIP alignment on 4 KB and 16 KB environments.
  If file-backed loading fails, evaluate a bounded LLVM object loader that maps local AOT output with separate writable
  and executable phases. Treat that as a separate feasibility decision because it adds a loader and relocations.
  [Android 10 execute restrictions](https://developer.android.com/about/versions/10/behavior-changes-10#execute-permission) ·
  [Android 14 dynamic loading](https://developer.android.com/about/versions/14/behavior-changes-14#safer-dynamic-code-loading) ·
  [NDK dynamic linker](https://developer.android.com/ndk/reference/group/libdl) ·
  [16 KB page sizes](https://developer.android.com/guide/practices/page-sizes)
- **Storage and build cost.** The full ISO copy alone adds 1,459,978,240 bytes. Measure peak storage for ISO staging,
  tools, analysis, C/object files and two module generations, plus peak RSS, elapsed time and thermal behavior on the
  S23 Ultra. Check free space before copying or compiling. Keep the first worker foreground-visible, cancellable and
  restartable. Keep RVZ conversion, additional ABIs and zero-copy URI paths for follow-up work after the arm64 ISO gate.
  [Storage Access Framework](https://developer.android.com/training/data-storage/shared/documents-files)
- **Updates and distribution.** Give the GitHub APK its own stable package ID, secure release signing key and
  increasing version code. Existing local builds use per-installation tool-folder keys with the same legacy package ID,
  so a public key cannot update those installs in place. Provide an explicit save/settings/mod export/import path for
  migration, gated by tests on both apps. Scoped storage prevents reading the older app's private directory directly.
  Keep the verified ISO and user data outside rebuild staging. Keep setup offline after APK download, including
  validation and compilation. Verify Android backup and device-transfer exclusions for disc data and build products.
  Exclude those files from diagnostics and release assets as well.
  Retain the Git game-data guard and audit the APK contents from a clean checkout with no disc access.
  [App signing](https://developer.android.com/studio/publish/app-signing) ·
  [Scoped storage](https://developer.android.com/about/versions/11/privacy/storage)

## Milestones and acceptance criteria

| Milestone | Expected files or components | Pass when |
| --- | --- | --- |
| 1. S23 Ultra native-load probe | `tools/android/probe/`, `tools/android_build.py`, `android/AndroidManifest.xml.in`, minimal Android-hosted compiler/linker | Sideload a separate arm64 probe using the phone's package installer. On stock Android with USB debugging off and the network disabled, compile a synthetic C function to an app-private PIC ELF on the phone, call a packaged runtime helper from it, and verify a known return value. Force-stop and reopen, and get the same result from disk without recompiling. Record OS, target SDK, page size, linker error if any, and compilation time. A packaged synthetic ELF can isolate loading first, but only device-produced output passes this gate. No ISO or game UI needed. |
| 2. ISO import | Setup activity/JNI, `tools/extract_iso.py` validation rules and existing `disc.c` path | Select a stock ISO via the system picker, verify size/header/full SHA-1 and DOL/REL checks, and keep a readable private copy across restart. Reject wrong revisions, truncated/corrupted images and interrupted copies. Verify insufficient-space and picker-cancel recovery. |
| 3. Recompiler parity and module build | `tools/build_workdir.py`, pinned decomp-toolkit, `tools/recomp/`, `configure.py`, `runtime/include/recomp_table.h` | On the S23, run analysis, translation and compilation from the verified image. Compare normalized dispatch addresses, entry point, HLE metadata and recompiler report counts locally against the desktop pipeline with identical inputs. Verify module imports, ELF protections and 16 KB alignment. Record peak RSS, disk use and time. Kill/retry leaves a consistent staging state. |
| 4. Play and relaunch | `SonicRidersActivity.java`, `runtime/src/main.c`, `cpu.c`, Android host platform glue | After an offline phone-only install, launch to title and finish a race with touch and a controller. Check Vulkan rendering, audio, save/settings persistence and a supported mod. Relaunch after force-stop and reboot with no recompilation. Compare the same route to the existing locally built Android app. |
| 5. Update and package verification | Android packaging, internal build manifest, migration hooks and eventual release tooling | Install compatible and incompatible signed updates over a seeded install. Verify ISO, saves, settings and mods persist, only an invalid module rebuilds, and interruption or low space is recoverable. Verify migration from the existing PC-built app, clean-build APK provenance, offline setup, and the supported 4 KB/16 KB device matrix before considering a GitHub asset. |

For milestone 1, use the existing probe and a temporary package ID. First load one synthetic ELF staged into the exact
intended private directory to isolate Android policy. Then exercise the same path with a phone-linked synthetic object
if useful for isolating LLD, followed by phone compilation of the tiny C source. Advance only after the final case passes.

## Milestone 1 probe APK

- The probe reuses the Android SDK packaging helpers. Its separate package is
  `net.srnative.sonicriders.nativeprobe` (arm64-v8a, min SDK 30, target SDK 35). It declares no permissions.
  TinyCC is a probe-only compiler. Its exact source and license are inside the APK. The game toolchain is still
  an open selection. The synthetic module calls the packaged runtime helper through a C function pointer.
- Artifact under the worktree at `build/native-probe/RidersPlus-Android-Native-Probe-arm64.apk`. Install from
  the phone's Files app, enable airplane mode, turn off USB debugging, and open **Riders Plus Native Probe**.
  Expect **PASS**, **Compiled on this launch**, and result **49**. Copy or capture the diagnostics. Force-stop the
  probe from its App info screen, then open it again. Expect **PASS**, **Reused cached ELF**, and a new PID.
  A failure includes the compiler or linker error. Keep the app installed to preserve the cached ELF.
- On-screen storage shows the APK size, available space, and the ELF's logical and allocated bytes. The process
  peak RSS is a lifetime high-water mark, with before/after readings on first compile. It includes Android's
  process overhead. Compile time covers compilation, linking and writing the module.
- Developer rebuild: check out [the pinned TinyCC revision](https://github.com/TinyCC/tinycc/tree/e9e9e7f6236895a32c5e905aada486bab6671e95)
  under `build/native-probe/tinycc`, then run
  `py -3 tools/android/probe/build_native_probe.py --tools <setup-tools>`. The helper compiles TinyCC with
  embedded predefinitions using upstream's converter and signs with a separate local probe key.
- [confirmed] APK signature, 16 KB ZIP alignment, ARM64 manifest with target SDK 35, and empty permission list
  verified on the build host. A host build of the pinned compiler produced the expected synthetic AArch64 ELF
  with the exported entry, indirect helper call and separate executable and writable segments.
  [uncertain] Device policy, successful execution and cached reload, and the measured phone timings and RSS.
