# Experimental Switch 2 audio/stutter diagnostics (2026-11-18)

Branch codex/switch2-stall-diag. Validated Alpha1releasebranch64e8316/ZIP
unchanged; higherFPSb381e54 separate/excluded. No publication.

Scheduler fix5db73f5/build4a6ead9bbb02 is hardware-tested for SIXconsecutive
races at2x with zero character-selection freezes and cleanQuit/hbmenu. This is
promising evidence, not permanent elimination. Exactbinary+manifest preserved
build/alpha/scheduler-sixrace-4a6ead9bbb02/ and branch
codex/switch2-sixrace-checkpoint at5db73f5; SD backup retained on deployment.

Remaining: music lost race1, announcer lost byrace3 while ordinarySFX continue.
Race2/3 stutter, sampledmin32.3/28.2fps with long renderwaits and audiofeedgaps
up to3.569s, but recordedGPU/execute short. Cache/newpipe events earlier do not
explain sustainedstutter; pausedlaterraces okay. Rootcause/sharedrelation to
scheduler unconfirmed; no speculativefix. Sourceofmixedaudio versus guest
stream/voice/refill/waits needs new diagnostics. All19workerscleanexitconfirmed.
Evidence: build/alpha/condfix-six-races/focused-analysis.md and rawlogs.

Implemented next experiment: native-only bounded2sAXvoices/aggregates,5s guest
thread/DVD/DSP/IRQ/renderqueue/workerwait summaries, completedwait maximum and
timeouts, longinputpump logging. Retains successfulpredicate-recheckfix,
2xresolution, disabledlivechanges andframes0; no rendererpolicy/audio rate/FPS
changes. Snapshotsoverheadlogged; evidence is asynchronous and can perturbtiming.
Locallytested: schedulerregressionPASS, originalSwitchmain/sched/menu/AX
preprocessingunchanged, AndroidVksyntaxPASS, nativebuild/link/manifest/hashchecks
PASS. Build67582dd7f046 bytes29464525 SHA
4f1a317d3b7f182b7539e76a81e5f7df4e231b0b60b8da1112b9bb151fc26942.
Exactdeploymentresults recorded in AI handoff and build/switch2/audiodiag-deployment.json.
Hardwarepending. No releaseZIPrebuild/coldtest until hardwareacceptance.

Nexttest: normalentry, same race/menu/reselect route. Note approximate time of
music/announcer loss and stutter, pause/menustate and mode/clocks. Quit normally
when possible, preservelogs viaFTP. Analyzevoice2/stream2/wait2/vk2-loop alongside
existingaudio2/perf/cachetimers. Never assume rawvoicePB is namedmusic without
correlation. Preserveactiveconfig/ISO/saves/caches/rollbackbuilds.
