# Experimental Switch 2 state (2026-11-17)

This records feature/switch2-native. Public main/private worktrees remain untouched.
Do not push, merge, publish or release this branch.

## Accepted checkpoint

Build 7d1bc5fcd419 is hardware-tested and accepted for the user's tried gameplay,
input, graphics, music/sound, overlay, aspect settings, bloom and enhancements.
Preserve this exact binary. No runtime code or SDK changed during this review.
Firmware 23.0.1, handheld (mode 0), clocks unrecorded. No universal FPS guarantee.

## Confirmed diagnostics

- Raw handheld A maps to GC 0x0100 and is delivered through SI INBUF. The merged
  No1/Handheld reader works; retain input and overlay behavior.
- All 85 audio summaries: 48 kHz stereo PCM16, zero dropped samples/delivery
  errors. Final append/release balance leaves one queued buffer. Startup feed
  gaps exist; steady audible operation is user-confirmed. Exact cause of the
  previous garbling is not uniquely established by these logs.
- Worker, Vulkan, audio and memory cleanup and filesystem EXIT_HOOK complete.
  User confirms hbmenu return, relaunch and OunceShell.
- Warm startup prewarms 44/44 pipelines in 1 ms. A frame spends 6736.22 ms in
  synchronous cache saving with zero new pipelines and 0.03 ms actual present.
  Later cache saves and genuinely new pipelines cause smaller hitches.
- Cache-save timer includes serialization and SD writes; their split is unknown.
  Cached prewarm marks pipeline creation dirty and triggers render-thread saving.
  No loading optimization has been implemented or authorized in this review.

## Preserved artifacts

Exact accepted NRO SHA256:
ffa5034b038542e4da6b2530892642d3b1cee20d3bd1a9b69f5789825c7730a4

Local binary, manifest, env and downloaded logs:
build/switch2/accepted-7d1bc5fcd419/ (ignored/private, never upload or commit).
Existing build/switch2/sonicriders-switch2.nro remains byte-identical.
The console env.txt remains SR_SWITCH2_FRAMES=9000; no FTP writes or rebuild.
Old ZIP packages contain stale configuration; do not deploy them.

## Next steps and remaining limits

No critical correctness issue is shown before controlled extended play on this
accepted build. Preserve caches for warm comparisons. Keep input/audio/graphics
unchanged. Before a startup fix, measure cache serialization versus file I/O,
then consider reducing/deferred saves or prewarm-only dirty bookkeeping.
Report findings and recommendations before implementing such a change.

Existing 9000-frame cap limits session length; the user can adjust it when ready
for longer sessions. Leave it unchanged unless requested. Other stages/story,
multiplayer, docking, sleep/resume, long-run stability and settings persistence
still need coverage. Use log-reader for future logs and record mode/clocks.

Architecture and build commands: docs/notes/switch2.md. The verified SDK is
ounce-devkit20261117. Do not rebuild the accepted artifact just to refresh a Git
version string. Source hashes link it to the local checkpoint independently.
