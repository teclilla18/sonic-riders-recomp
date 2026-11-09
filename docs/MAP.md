# Code map

Build/run commands: [notes/build-and-test.md](notes/build-and-test.md) "Quick commands". Doc index: [REFERENCE.md](REFERENCE.md).

Where things live, so a change starts with `grep -n '<name>' <file>` instead of reading whole files. Names are
functions or macros (line numbers drift). Sizes are approximate line counts.

## Runtime core (`runtime/src/`)

| Topic | File | Look for |
|---|---|---|
| Boot, threads, main loop | main.c (150) | `main` |
| Guest memory, address helpers | runtime.h, `runtime/include/ppc.h` | `ld32`, `mem_phys` |
| Game addresses (modes, players, gear table) | game_addrs.h | `GAME_*` |
| Frame pacing, 30/60/Auto, RAM hash/dump tools | frame_pacing.c (490) | `ram_hash`, `set_steps`, `SR_RAMDUMPAT` |
| Thread scheduler (guest threads) | sched.c (620) | |
| GX FIFO parse, draw decode | gx_fifo.c (760), gx_render.c (870) | `decode_vertex`, `gx_draw` |
| Textures | gx_texture.c (410) | `gx_texture_get` |
| Shader keys | gx_shader_key.c | |
| Audio (AX / DSP) | ax.c (470), dsp.c (440), audio_out.c | |
| Disc / DVD | di.c, disc.c | |
| Memory card, RTC | exi.c (430) | |
| Controllers into the game | si.c; scripted input input_script.c (`SR_INPUT`) | |
| Settings file | settings.c / settings.h | `g_settings`, `settings_apply` |
| Settings menu (ImGui) | menu.cpp (760) | `tab_display`, `tab_controls`, `quit_row`, `close_menu`, `overlay_input`, `overlay_pointer`, `overlay_image` |
| Menu software rasterizer | menu_raster.cpp | |
| Mods | mods.c, mods_data.c, game_mods.c | |
| Online / lockstep | netplay.c (600) | `SR_NET` |
| Perf test | perftest.c | `SCENE_KEY` |

## Platforms (`runtime/src/platform/`)

| Topic | File | Look for |
|---|---|---|
| PC + Android input (SDL pads, keyboard, triggers) | host/input_sdl.c (260) | `stick8`, `trigger`, `read_gamepad`, `input_pump` |
| PC renderer (GL) | host/render_gl.c (1250) | `efb_config`/`rescale_copies` (resolution, MSAA), `post_process` (bloom, grading), `fsr_present` (FSR 1 EASU + RCAS at present; shaders in host/fsr1_gl.h, AMD MIT), `sync_fullscreen` (also the background-controller hint), `execute`; frame rate presets: `ip_round`, `ip_match` (in-between pictures), menu.cpp `pc_frame_rate`, launcher `fps_selector` |
| PC/Android platform glue | host/platform_host.c | |
| Android renderer (Vulkan) | android/render_vk.c (1720) | `vk_setup`, `execute`, `spec_bits` (specialized shaders), `pipe_get`, `pcache_save`, `draw_overlay_and_touch`, `picture_rect`, `shot_request`, `pump`, `render_thread` |
| Android touch controls | android/touch_android.c (270) | `layout` (positions), `stick_home`, `stick_value` (dead zones), `game_in_race`, `touch_event`, `touch_apply`, `touch_shapes` |
| iOS glue | ios/ios_scene.m (35); tools/ios_build.py, tools/ios_run.py | `ios_attach_window` (UIScene, iOS 27); Xcode project, Info.plist, `flatten_png` (icon); devices/install/run/pull |
| iOS player setup / release | tools/setup_mac.py; tools/make_release.py (`--flavor ios`, `IOS_COMMAND`, `readme_ios`) | 7-step Mac setup; `ios_build.find_team` (cert, else Xcode account), unsigned .ipa; main.c `ios_message` (disc missing / incomplete) |
| Mac launcher (iOS builder) | launcher/mac/SonicRidersLauncher.swift, launcher/mac/START_HERE.html; tools/build_mac_launcher.py; make_release.py `build_mac_launcher` | pages `ReadyPage` (`Model.recheck`), `BuildPage` (`Model.build`, `Model.line` progress), `InstallPage`, `RenewPage`; `findRoot`/`originalURL` (translocation); icon: ios_build.py `square_png`, `flatten_png`, `make_icon`; setup_mac.py `prepare_icon` |
| Touch control drawing (shader) | `runtime/shaders/vk_touch.frag` | `icon()` (per-button strokes) |
| Android Java activity | `android/java/net/srnative/sonicriders/SonicRidersActivity.java` | `onCreate` (crash report before SDL starts) |
| Android crash report (next launch) | `android/java/.../CrashReport.java` | `collect` (exit record + crash.txt + log → Download/SonicRiders), `show`, `share` |
| Native crash handler (Android, Linux) | host/platform_host.c | `crash_signal` (crash.txt, logcat, log), `crash_install` |
| Android manifest template | `android/AndroidManifest.xml.in` | `@ICON@` |
| Switch input | switch/input_hid.c | |
| Switch renderer (deko3d) | switch/render_deko.c (1100) | |
| Switch platform | switch/platform_switch.c | |

## Shaders (`runtime/shaders/`)

gx.vert / gx.frag / gx_common.glsl: the shared GX shader (CLAUDE.md GLSL rules apply). overlay.*: menu image.
gx_light.glsl: the lighting functions, pasted into gx.vert (`//@gx_light`) and, for PC only (PERPIXEL_LIGHT),
into gx.frag (per-pixel lighting); tools/embed_shaders.py `source()`.
vk_*.frag / vk_quad.vert: Vulkan helpers (copies, fills, touch controls).

## Build and tools (`tools/`, root)

| Topic | File |
|---|---|
| Build file generator (all targets) | configure.py (`--target host/switch/android`, `--icon`) |
| Android APK steps (no Gradle), icon into resources | tools/android_build.py (`step_apk`, `add_apk`) |
| Android app icon | tools/android_icon.py |
| Phone test runs over adb | tools/android_run.py |
| PC icon from the save icon | tools/make_icon.py |
| Touch pictures into a header | tools/embed_icons.py (`decode_png`) |
| Recompiler | tools/recomp/gcrecomp.py, ppc2c.py |
| Game-data guard | tools/check_no_game_data.py (`OWN_ART`) |
| Setup (Windows) | setup/setup.ps1, setup/toolchain.json |
| Perf test compare | tools/perf_test.py |
| Game config | config/GXEE8P/hle.txt, patches.txt |
