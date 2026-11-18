# Native audio/pump diagnostics (2026-11-18)

Branchcodex/switch2-stall-diag. Sixrace scheduler-fixcheckpoint5db73f5/4a6ead
preserved, plus3more67582races withoutselectionfreeze; promising, not permanent
elimination. Validatedrelease64e8316 ZIP unchanged, no publication.

67582race1 hitchlocalizedinput/eventpump694ms, subsequent350/224/141ms;
voicePCMcountsfallthenreturn, loaderthreads laterresume. Music/announceruserloss
persists whilegameplayrecovers: identity/decoderfailureNOTestablished. NoGPUloss
or permanentguestdeadlock shown. SharedlogIOblocking possible, unmeasured.
Evidence build/alpha/audio67582-hw1/focused-analysis.md; allrawlogspreserved.

Implemented3e551c8bf7f3 diagnosticsONLY: preciseapplet/drawable/inputpumpsplit,
pad/overlay/heartbeat split, sharedstderrwrite/flushmaxlatency counters, PCM
voiceprioritysnapshot inclstopped (8PCM+8other) for before/aftercomparison.
No speculativefix/audio-rate/FPS/cache/rendererrefactor. Retains successful
schedulerfix,2x,livecontrolsdisabled,frames0. OriginalSwitchpreprocessing and
Androidsyntax, schedulerregression, nativeartifactsourcefreshness/linkchecksPASS.
Bytes29468621 SHAcd7851aafbb9bac6fca662e3d2e5e8b24966af217740cb1730c29db0dce632a9.
Deploymentstatus in handoff and build/switch2/audio2-deployment.json.

Next: normalentry same races; noteexactelapsedtimefirstmusic/announcerloss and
stutter. Capture logs before relaunch. Correlatelog2/pump2/input2-time with
voice2PCMtransitions and stream2/workerqueueprogress. No timingnumbers from
softwaretests representhardware; snapshots asynchronous/perturbationpossible.
NoZIPregeneration/coldtest until actualfixhardwareaccepted. Preserve allrollback
branches/artifacts and isolateddata. No externalreplies/push/merge/publication.
