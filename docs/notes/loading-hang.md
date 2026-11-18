# Rare loading hang (#6)

Issue [#6](https://github.com/teclilla18/sonic-riders-recomp/issues/6) predates the
Android Vulkan fallback. Keep loader investigations separate from #32's verified
boot/race/shutdown compatibility fixes; do not ship speculative loader changes.

## Earlier evidence

The October 16 PC/Eden captures in
[the historical research](../archive/history-2026-10-16_to_25.md) show a completed
final file-tail read, idle DI and no pending interrupts. The CRI consumer spins
at 8194DD4C through parser 819594F4 returning not-ready; loader 81A93B68 idles at
8194DF20 while the game waits for the stream. Lost resume and completion/interrupt
ordering were leads, not established causes. Later progressive 16 KiB disc DMA
fixed a confirmed stale-data decompression bug and reduced the suspected hang;
Issue #6 remained open. Do not treat those historical leads as proven fixes.

## Mali report, 2026-11-18

The [Green Cave/Tails report](https://github.com/teclilla18/sonic-riders-recomp/issues/32#issuecomment-6992322743)
contains [the original attached evidence](https://github.com/user-attachments/files/35054699/Sonic-Riders-now-loading-hang.log).
Its SHA-256 is
`8b0e7616e46bcd9c1de89b9acb391c7c1bf196ebfbf2e8193ba567214d1750f0`.
The local immutable copy and provenance manifest are excluded from Git; retain them.

- **Confirmed:** build 90868613 on Mali-G57 MC3 / Vulkan 1.1.177, automatic legacy
  rendering, separate alpha writes and descriptor-pool fallback. The race completes.
- **Confirmed:** after race-end/save activity, rendering drops to four draws/frame
  around frame 30,600 and continues through 38,400, roughly 130 seconds. Simulation
  and rendering stay near 59.9/s. GPU time is about 7 ms; render wait 0.04-0.05 ms.
  Disc traffic stays at 54 MB with zero further completions.
- **Confirmed:** no Vulkan error, device loss or blocked GPU is evidenced. Later
  quit completes every cleanup/application-handler phase without a subsequent signal.
  The separate Splash Canyon/Amy log on the same build also ends with clean cleanup.
- **Likely:** a guest loading/game-state progress failure, consistent with the old
  #6 symptom class. Working render/present cycles and low waits argue against a
  Vulkan stall. The hang precedes quit, so shutdown gating/teardown has not begun.
- **Uncertain:** identical underlying cause to #6. The log contains no watchdog
  guest stacks, DI/IRQ state, CRI readiness predicate or loader suspend/resume trace;
  it cannot establish the old exact signature or exclude every timing interaction.

Source audit: the #32 commits do not change di.c, sched.c, hle_os.c or
frame_pacing.c. New shutdown gates and application exit handlers activate only on
quit. No evidence establishes a new rendering/shutdown regression here. Treat the
loading hang as a separate open investigation; its exact attribution is pending.

## Next investigation

1. Reproduce race completion with Tails on Green Cave, retaining mode/settings and
   whether returning to menus or retrying. Compare old/new builds and both rendering
   paths if reproducible; normal timing, no SR_DETCLOCK.
2. Use existing `SR_WATCHDOG=<seconds>` to capture guest threads, DI/DSP state and
   the event ring during the hang. First try `SR_WATCHDOG_NOTRACE=1` to reduce the
   chance of masking a timing race; a watchdog intentionally terminates the app.
3. Compare last read completion/interrupt delivery, loader suspend/resume, CRI
   readiness and game wait predicates with the historical signature. Diagnose a
   concrete invariant failure before changing disc/loader/scheduler behaviour.
4. Preserve saves and raw captures locally. A non-reproduction or clean quit is
   not evidence that the loading hang is fixed.
