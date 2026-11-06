# iOS / iPadOS port: research (2026-11-05)

**Investigation.** How to build and install the Sonic Riders recomp on iPhone/iPad as an app (.ipa). From public
docs, forums and guides, read 2026-11-05; nothing here was tested by us. The user owns an M1 MacBook Pro, so the
plan is a Mac build. Test setup: iPhone 17 Pro on iOS 27.0; MacBook Pro M1 Pro on macOS Tahoe 26.5.1; free Apple
account (7-day reinstall). Xcode version: not recorded yet.
Index: [REFERENCE.md](../REFERENCE.md).

## Verdict (short)

- **No JIT needed.** The port is ahead-of-time C, so it runs like any normal signed app. This avoids what blocks
  emulators: iOS 26 broke the existing JIT enablers (DolphiniOS changelog) [confirmed by source].
- **Build on the Mac with Xcode**, sign with the user's Apple ID, install over USB/Wi-Fi with `devicectl`.
- **Graphics: reuse the Android Vulkan renderer** (`runtime/src/platform/android/render_vk.c`) over MoltenVK
  (Vulkan on Metal) [likely].
- **Game-data rule:** `build/gen` must be made on the user's machines (PC recomp copied to the Mac locally, or the
  recomp run on the Mac). No CI build on GitHub macOS runners: that would upload recompiled code.

## Toolchain

- Xcode 26.0/26.0.1: macOS Sequoia 15.6+. Xcode 26.4 and later: macOS Tahoe 26.2+ (Apple requirements page,
  Chinese mirror of release notes). The M1 supports Tahoe. Xcode must support the device's iOS version.
- **iOS 27 device needs Xcode 27** (iOS 27 SDK, deployment iOS 15-27, Apple silicon only). Xcode 27 needs
  **macOS Tahoe 26.6+** (secondary sites citing Apple; floor unchanged through Xcode 27.1, Oct 2026). The test Mac
  is on 26.5.1: update macOS first. On-device debugging needs iOS 17+.
- SDL2 for iOS: CMake 3.14+ with `-DCMAKE_SYSTEM_NAME=iOS`, `-DCMAKE_OSX_SYSROOT=iphoneos` (or
  `iphonesimulator`), `-DCMAKE_OSX_ARCHITECTURES=arm64`; SDL2main optional, before SDL2 in link order
  (SDL docs/README-cmake.md). Likely shape for us: `configure.py --target ios` generating an Xcode project.
- SDL's Vulkan surface needs a CAMetalLayer-backed view; old SDL builds failed `SDL_Vulkan_CreateSurface()` until
  that was fixed. Use a current SDL2.
- MoltenVK: link the static `MoltenVK.xcframework`; runtime needs iOS 13+. Whether the current LunarG Vulkan SDK
  ships an iOS MoltenVK build is not confirmed; fallback is building MoltenVK from source on the Mac.
- Shaders: the Android path's SPIR-V goes through MoltenVK's SPIRV-Cross to Metal [uncertain until tried].

## Install and test loop

- `xcrun devicectl list devices` -> `xcrun devicectl device install app --device <id> <App>.app` ->
  `xcrun devicectl device process launch --device <id> <bundle id>` (`--terminate-existing` to relaunch).
  devicectl is Xcode 15+. `xcodebuild -destination` wants the hardware UDID, not the devicectl id; building for a
  generic iOS device avoids that.
- No devicectl screenshot command; libimobiledevice `idevicescreenshot` can grab frames. Port captures
  (`SR_SCREENSHOT`) remain the preferred way to look at frames.
- Apple Silicon Macs can run the iOS build directly ("My Mac (Designed for iPad)" in Xcode); a macOS 14.4 bug with
  that destination was reported. Useful as a first test without a phone [uncertain for MoltenVK].
- Bonus: the same SDL2 + MoltenVK stack gives a native macOS build (memory.c already uses `mmap`).

## Signing

- **Free Apple ID (Xcode Personal Team):** profile expires after 7 days; reinstall from Xcode. Apple publishes no
  exact quotas; the often-quoted "3 apps" limit is unconfirmed. Push and app extensions unavailable.
- **Paid account ($99/yr):** 1-year profiles, more capabilities. The Increased Memory Limit entitlement likely
  needs paid in Xcode (no source says a free team can add it); AltStore 2.2 signs it on free accounts. Probably not
  needed: guest MEM1 backing is 28 MiB; the 512 MiB guest window is a PROT_NONE reserve (fine on iOS; Apple's
  extended-virtual-addressing entitlement exists if not).
- **Other users later:** build their own on a Mac, or an unsigned IPA (`Payload/<App>.app` zipped) signed with
  SideStore + LiveContainer (free account, 7-day refresh; iOS 26.4 broke refresh, 26.4.1/nightlies reported fixed;
  one article says the LiveContainer GitHub repo was hidden from 2026-10-04, unconfirmed). TrollStore: old firmware
  only. EU marketplaces (AltStore PAL etc.) are for distributed apps, not per-user builds.

## Open items

- Update the Mac to Tahoe 26.6+, install Xcode 27.x; record the Xcode version.
- Input: port the Android touch overlay; controllers through SDL GameController.

## Sources

- DolphiniOS 4.1.0 beta 3 (iOS 26 JIT): https://provenance.itch.io/icube/devlog/1025411/dolphinios-410-beta-3-for-ios-and-tvos
- Xcode requirements: https://developer.apple.com/xcode/system-requirements ; 26.4 notes mirror: https://developer.aliyun.com/article/1719435
- Xcode 27 requirements: https://blakecrosley.com/blog/xcode-27-release ; https://mjtsai.com/blog/2026/10/16/xcode-27-1/ ; https://macyou.co/xcode-system-requirements
- SDL README-cmake: https://skia.googlesource.com/external/github.com/libsdl-org/SDL/+/162f8e6ee75ba1921060dbd9cf500dec29082e21/docs/README-cmake.md
- SDL Metal layer fix: https://git.axiodl.com/encounter/SDL/src/commit/959ae9019d36ae88a16aa01829ea7d122d249696
- Vulkan on iOS (Khronos): https://www.khronos.org/blog/developing-with-vulkan-on-apple-ios
- MoltenVK runtime user guide: https://github.com/SRSaunders/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- devicectl install guide: https://flaviocopes.com/install-iphone-apps/
- Designed for iPad on Mac: https://developer.apple.com/documentation/apple-silicon/running-your-ios-apps-in-macos.md
- Free provisioning 7 days: https://developer.apple.com/forums/thread/93183 ; quotas undocumented: https://developer.apple.com/forums/thread/850133
- Increased memory limit: https://developer.apple.com/forums/thread/729679 ; AltStore 2.2: https://www.idownloadblog.com/2025/04/16/altstore-v2-2-released/
- Extended virtual addressing: https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.extended-virtual-addressing.md
- SideStore + LiveContainer on iOS 26: https://ipswdl.com/blog/post/how-to-install-sidestore-livecontainer-on-ios-26/ ; https://gist.github.com/sinceohsix/688637ac04695d1ff38f844acc8ba7f3
