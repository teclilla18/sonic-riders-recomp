# PS4 / PS5 port: research (2026-11-03)

**Investigation.** What the console homebrew scene offers as of 2026-11-03, for a possible port after Android.
From public news and GitHub pages, not tested by us. Firmware support changes fast: recheck before acting.
Index: [REFERENCE.md](../REFERENCE.md).

## PS5 jailbreak status

- Public: **"Relapse"** (browser + kernel exploit, kernel read/write) covers firmware **7.00-13.60**, standard
  and Pro. Homebrew enabler on top: **etaHEN**; payloads via an ELF loader on port 9021 (etaHEN, elfldr);
  **kstuff**; **ShadowMountPlus** puts homebrew titles on the home screen (`/data/homebrew/<id>`).
- 14.00 (mid-September 2026) broke Relapse; **14.10** reportedly exploited in private (Gezine), not released, the
  hypervisor still holds. Reports: modified consoles can no longer reach PSN below 14.10.
- So: a console on ≤ 13.60 that stays offline can run homebrew today; a console on 14.x cannot (yet).

## PS5 native graphics: open Vulkan now exists (this changes the old plan)

- **PS5_Vulkan** (github.com/mihawk-99/PS5_Vulkan, GPL-3.0, updated 2026-10-01): a port of Mesa's **RADV**
  Vulkan driver to the PS5 GPU, reporting Vulkan 1.4 (not conformant yet; second full CTS run: 1.57 M passes,
  0 failures). Swapchain through `VK_KHR_display` on VideoOut, vblank flips, 120 Hz where supported. The driver is
  linked statically into the title (the console refuses to load our own `.so` at run time). Built on the public
  **PS5 Payload SDK** (github.com/ps5-payload-dev/sdk) with clang, on a Linux host. A second driver, **ps5vk**
  (Mesa-derived, Vulkan 1.1), exists. Limits: no async compute, coarse occlusion queries, no buffer device
  address, pipelines compiled on first use (cache in `radv-shader-cache/`).
- In use: **PS5_RetroArch** (Dolphin, PPSSPP, Flycast, PCSX2 cores on the GPU, e.g. PPSSPP at 6x with 8x MSAA),
  **Porpoise** (Dolphin for PS5), **RecompStore** (homebrew store for recompiled ports; "bring your own disc":
  the store puts the player's own image where the port expects it; no game files hosted).
- Recomp ports on PS5 already: Xenoblade Chronicles (KongaTime/XBC-Recomp-PS5-Port: 1080p60 in the tested
  battle, player supplies NSP + update + keys, preparation on the console; no binary published because the
  executable contains generated game code), Spider-Man: Edge of Time (via etaHEN + ShadowMountPlus).
- **What it means for us:** `platform/android/render_vk.c` is already a Vulkan 1.1 renderer of the GX state
  (with specialized shaders). A PS5 target = render_vk.c with a `VK_KHR_display` swapchain instead of SDL's
  surface + PS5 pad/audio/file glue from the payload SDK. Much closer than the "GLES Piglet" route assumed before.
- Our rule stays: the PS5 build is made on the player's PC from their own disc (like the Switch/Android builds);
  nothing generated is published. The XBC port's "no binary because it contains generated code" matches that.

## PS4 status

- Jailbreak: public up to firmware **13.52** (WebKit + kernel, GoldHEN, semi-untethered; offline). One report
  (2026-11-02) says a 14.00 WebKit exploit and GoldHEN 2.4b18.13 with 14.00 offsets are out: unconfirmed.
- Graphics for homebrew: OpenOrbis toolchain; the system's GLES 2 library ("Piglet", no GLSL source, shaders must be
  precompiled) or **PS4-OpenGNM** (github.com/PS4-OpenGNM: clean-room GNM rewrite, SPIR-V → PS4 shader compiler
  on Mesa; updated 2026-10). No RADV-style Vulkan for PS4 found. Jaguar CPU: slower per core than the overclocked
  Switch.
- A PS4 fpkg would also run on a PS5 in backward compatibility, but a native PS5 build (above) is now the better
  PS5 route.

## Suggested order (unchanged in spirit, PS5 moved up)

1. Picture enhancements on PC, then Android/Switch (resolution scale, bloom, HD texture replacement by hash).
2. **PS5 probe**: payload SDK + PS5_Vulkan (RADV) in the setup, a small ELF that clears the screen through
   `VK_KHR_display`, reads a pad, plays a tone and runs render_vk.c's 7,000-draw test. Needs a PS5 on ≤ 13.60
   with etaHEN (user's hardware: ask).
3. PS5 port proper (render_vk.c shared with Android). PS4 only if wanted after that (GLES/OpenGNM is a
   separate renderer).

## Sources

- PS5 firmware: tomshardware.com (Relapse 13.60), techpowerup.com (14.10 private), notebookcheck.net (PSN 14.10).
- github.com/mihawk-99/PS5_Vulkan, github.com/mihawk-99/PS5_RetroArch, github.com/elripalda/RecompStore,
  github.com/KongaTime/XBC-Recomp-PS5-Port, github.com/ps5-payload-dev/sdk, github.com/PS4-OpenGNM.
- PS4 firmware: consolemodding.it (13.52 guide), onejailbreak.com (14.00 claim).
