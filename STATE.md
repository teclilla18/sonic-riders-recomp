# Personal Switch 2 Unlimited FPS experiment (2026-11-18)

Branchcodex/personal-switch2-unlimited from published1df03e0. User requested
personalUnlimitedoption only; no publicrelease/source/main/tag changes.
Reuse priorb381 render interpolation/safe4frameownership and fence retirement,
retaining currenttested scheduler/asynclogging fixes. Native overlay adds only
Unlimited rendering toggle inPerformance; defaultsOff/normal60, savedpc_fps=-1
whenenabled. Selecting30 clearsUnlimited. Game/retrace/audio clocksunchanged.
No live resolution changes;2x andframes0/userconfigpreserved. Supported
presentationmode/refresh/GPU can cap visible rate; Unlimited removes software
renderwait, not a promise ofunlimitedphysicalrefresh. Immediate cantear.

Local actualscheduler/rate/pairing/retirement/transition tests PASS. Native
build/link/manifest/sourcefreshness andexistingplatformcompatibilityPASS.
Personalbuild674d9c409e12,29477417bytes SHA
c6a8a6b92edacfdc4410f4f0b316195672f0eae18e75d5f4cb2635bbbea61362.
Hardwareexperiment pending; earlierFPSwork was not validated after scheduler
fix, so do notclaimaccepted. Published Alpha1ZIP and all rollbackspreserved.
FTPdeploymentresult inhandoff/build/switch2/personal-deployment.json.
Next: toggleUnlimited duringrace, verifygame speed/input/audio, return60 and
Quit/relaunch persistence; reportmode/clocks. RevertNRO via backedupAlpha1pair
ifissues; setpc_fps0 if reverting. No publication/remote pushes.
