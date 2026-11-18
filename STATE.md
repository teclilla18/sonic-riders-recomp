# Experimental native logging-stall fix (2026-11-18)

Branchcodex/switch2-stall-diag; release64e8316 ZIP unchanged, sixrace scheduler
checkpoint5db73f5 preserved. Subsequenttests still noselectionfreezes; notproof
permanentelimination. Keep2x/livechangesdisabled/frames0 and alluserdata.

3e551results/menu stutterconfirmedsharedlogwrite/flush:118/817/982/4162ms
heartbeat stalls correlate matchinglogIOduration, pads/overlay/applets0ms.
PCMstereo stops/reuses; mono laterrecoversandstopswithoutbigstutter. Logging
fixdoesnotestablishmusic/announcerrootcause; no decoder/forcedrestartfix.
Evidence build/alpha/audio3e551-hw1/focused-analysis.md +allrawlogs.

Implementednativeboundedroutine-logqueue with nonblockingproducer(dropful/
contention), FIFOwriter awayguest/rendercores. Writeroutlivesmanagedworkers;
checkpoint/fatal/shutdownflushbarrierspreserveacceptedrecordorder. Persistent
logsretain backgroundIOtiming and visible drops. No SDIO underqueuelock.
Loggingmaydropdiagnosticrecords underpressure ratherthanblockgameplay.

Buildc8c06fdda996 bytes29468857 SHA
1a9fc3aa7ad2f072eeac4d6a10110c17c6617c8f6611084f4b97352d9af992a2.
LocalqueueFIFO/capacity/wrap/truncation/seqtestsPASS; schedulerregressionPASS;
originalSwitchpreprocessing/AndroidsyntaxPASS; nativeartifactsource/linkPASS.
Hardwareacceptancepending includingflush/cleanshutdown. Deploymentstatus recorded
in AIhandoff/build/switch2/logfix-deployment.json. Neverclaimmusicfixedyet.

Nexthardware: results/menu same route, characterselect/back; announcerrecovery
andlaterloss. Report stutter/audioeventtimes. Quitnormallyandreturnlogs. Lookfor
[log2-queue]startupenabled anddrops, backgroundlogIOslow vs inputpumpfast,
PCMreuse/gueststreamstate. If writerfallbacklog appears, reportit. Testreturnto
hbmenu/relaunch. No releaseZIPregeneration/coldtest/publication beforeapproval.
