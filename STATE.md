# Switch 2 Alpha 1 final publication candidate (2026-11-18)

User priority: playable alpha released ASAP, not bug-free. STOP optional fixes,
features, optimization and extensive diagnostics. No publication before approval.
Branch codex/switch2-alpha1-final from tested fa2ec57/c8c06fdda996. Runtime/config
must match that baseline exactly. Experimental branches/checkpoints preserved.

Hardware: scheduler wakeup fix plus bounded asynchronous logger tested across
consecutive races without selectionfreeze; permanent elimination not claimed.
Latest2races: first two temporary stalls/recoverable distortion, second otherwise
flawless/minor selection audio glitch. Logs preserved build/alpha/logfix-c8c06-latest
ONLY; no investigation. Known intermittent stutter, music/announcer dropout,
temporary distortion and startup/cache-save hitches remain documented.

Fresh2x/60fps/no automatic exit, live resolution controls disabled. Preserve
settings, ISO, saves and caches. HigherFPS remains separate/excluded. Runtime
unchanged; update release notes/metadata/audit to latest tested baseline.

Regenerate ONLY Switch2 setup ZIP using tools/make_release.py from clean guarded
source; audit every entry with tools/audit_switch2_release.py, exact runtime
fa2ec57 comparison. Final cold Sandbox --flavor switch2 --space, no --cache,
GUI thenCLI through exactZIP/MOTW/Explorer, ownISO readonly, results-only writable.
Existing approval covers validation/retest; minimized established workflow.
Record exact sourcecommit/size/checksum/results in final handoff/candidate record.
Do not repeat prior research or optional tests. No other platform packages,
stable-release changes, push/merge/remote tag/upload/announcement before approval.
Easy-read public artifact republish needs external access; local guides updated.
