#!/usr/bin/env python3
"""Performance record: a fixed, repeatable run (SR_PERFTEST=1, runtime/src/perftest.c), on PC or Switch.

The game boots on a fresh memory card kept in memory (your save is not touched), ignores the controllers,
skips the intro movie and measures 30 s of each of the title screen's two demo races (Metal City 1 player,
then a 2-player split-screen race). See docs/PERF.md.

  python tools/perf_test.py run [--runs 3] [--exe build/host-mingw/sonicriders.exe] [--save perf/results/X.json]
                                [--expect perf/results/Y.json]
      PC: runs the game N times (minimized, no sound) and prints the table.
  python tools/perf_test.py show FILE [FILE...] [--expect perf/results/Y.json] [--save perf/results/X.json]
      Tables of result files: perftest.json files written by the game (e.g. copied from the Switch's SD card,
      sdmc:/switch/sonicriders/perftest.json, after a run with SR_PERFTEST=1 in env.txt), or saved records.
      Several perftest.json files are combined like the runs of `run`.

--expect compares each scene with a saved record and prints "SLOWER than the record" when the game speed or the
frame rate is lower by more than --tolerance (default 1.0 per second), or the game thread's mean work per step
is higher by more than 10% (and 0.2 ms). That is a check across sessions: same machine, same settings (the
record says which); a difference inside run-to-run spread is not a result.
"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
METRICS = [("speed", "speed", "%.2f"), ("fps", "fps", "%.2f"), ("game_ms_mean", "game ms mean", "%.2f"),
           ("game_ms_p95", "p95", "%.2f"), ("game_ms_max", "max", "%.2f"), ("steps_over_16ms", "steps >16.7ms", "%d")]


def run_pc(exe, timeout):
    out = os.path.join(ROOT, "build", "perftest.json")
    if os.path.exists(out):
        os.remove(out)
    env = {k: v for k, v in os.environ.items() if not k.startswith("SR_")}
    env.update(SR_PERFTEST="1", SR_MINIMIZED="1", SR_NOAUDIO="1", SR_PERFTEST_OUT=out)
    log = open(os.path.join(ROOT, "build", "perftest_run.log"), "w")
    t = time.time()
    p = subprocess.Popen([exe], cwd=ROOT, env=env, stderr=log, stdout=log)
    try:
        p.wait(timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        sys.exit("the run did not finish within %d s (build/perftest_run.log)" % timeout)
    if not os.path.exists(out):
        sys.exit("no results (build/perftest_run.log)")
    r = json.load(open(out))
    print("  run done in %.0f s" % (time.time() - t))
    return r


def combine(runs):
    """runs: perftest.json dicts -> a record with the runs and per-scene mean/min/max"""
    first = runs[0]
    rec = {"version": 1, "build": first["build"], "platform": first["platform"], "settings": first["settings"],
           "runs": runs, "summary": {}}
    for scene in first["scenes"]:
        rec["summary"][scene] = {}
        for key, _, _ in METRICS:
            vals = [r["scenes"][scene][key] for r in runs if scene in r["scenes"]]
            rec["summary"][scene][key] = {"mean": sum(vals) / len(vals), "min": min(vals), "max": max(vals)}
    return rec


def load(path):
    d = json.load(open(path))
    return d if "summary" in d else combine([d])


def table(rec, title):
    print("\n%s: build %s, %s, frame rate %s, resolution %s, %d run(s)" % (
        title, rec["build"], rec["platform"], rec["settings"].get("framerate"), rec["settings"].get("resolution"),
        len(rec["runs"])))
    print("  %-8s" % "scene" + "".join("%20s" % name for _, name, _ in METRICS))
    for scene, m in rec["summary"].items():
        cells = []
        for key, _, fmt in METRICS:
            v = m[key]
            c = fmt % v["mean"] if key != "steps_over_16ms" else "%.1f" % v["mean"]
            if len(rec["runs"]) > 1:
                c += " (%s-%s)" % ((fmt % v["min"]).strip(), (fmt % v["max"]).strip())
            cells.append("%20s" % c)
        print("  %-8s" % scene + "".join(cells))


def expect(rec, ref_path, tol):
    ref = load(ref_path)
    print("\nAgainst the record %s (build %s, %s):" % (os.path.relpath(ref_path, ROOT), ref["build"], ref["platform"]))
    if ref["platform"] != rec["platform"] or ref["settings"] != rec["settings"]:
        print("  note: the record was made on %s with %s; this run: %s with %s" % (
            ref["platform"], ref["settings"], rec["platform"], rec["settings"]))
    bad = 0
    for scene, m in rec["summary"].items():
        r = ref["summary"].get(scene)
        if not r:
            continue
        notes = []
        for key, name in (("speed", "speed"), ("fps", "fps")):
            d = m[key]["mean"] - r[key]["mean"]
            if d < -tol:
                notes.append("%s %.2f vs %.2f" % (name, m[key]["mean"], r[key]["mean"]))
        w, rw = m["game_ms_mean"]["mean"], r["game_ms_mean"]["mean"]
        if w > rw * 1.10 and w - rw > 0.2:
            notes.append("game thread %.2f ms vs %.2f (+%.0f%%)" % (w, rw, 100 * (w / rw - 1)))
        if notes:
            bad += 1
            print("  %-8s SLOWER than the record: %s" % (scene, "; ".join(notes)))
        else:
            print("  %-8s as the record or better (speed %.2f/%.2f, fps %.2f/%.2f, game ms %.2f/%.2f)" % (
                scene, m["speed"]["mean"], r["speed"]["mean"], m["fps"]["mean"], r["fps"]["mean"], w, rw))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("run", help="PC: run the performance test")
    a.add_argument("--exe", default=os.path.join(ROOT, "build", "host-mingw", "sonicriders.exe"))
    a.add_argument("--runs", type=int, default=3)
    a.add_argument("--timeout", type=int, default=600)
    b = sub.add_parser("show", help="tables of result files")
    b.add_argument("files", nargs="+")
    for p in (a, b):
        p.add_argument("--save", help="write the combined record here (e.g. perf/results/<release>-pc.json)")
        p.add_argument("--expect", help="compare with a saved record")
        p.add_argument("--tolerance", type=float, default=1.0)
    args = ap.parse_args()
    if args.cmd == "run":
        runs = []
        for i in range(args.runs):
            print("run %d of %d (about 3 minutes; the game window stays minimized)" % (i + 1, args.runs))
            runs.append(run_pc(args.exe, args.timeout))
        rec = combine(runs)
    else:
        loaded = [json.load(open(f)) for f in args.files]
        raw = [d for d in loaded if "summary" not in d]
        if raw and len(raw) == len(loaded):
            rec = combine(raw)
        elif len(loaded) == 1:
            rec = load(args.files[0])
        else:
            for f, d in zip(args.files, loaded):
                table(load(f), os.path.basename(f))
            rec = None
    if rec:
        table(rec, "Result")
        if args.save:
            os.makedirs(os.path.dirname(os.path.abspath(args.save)), exist_ok=True)
            json.dump(rec, open(args.save, "w"), indent=1)
            print("\nsaved %s" % args.save)
        if args.expect:
            sys.exit(1 if expect(rec, args.expect, args.tolerance) else 0)


if __name__ == "__main__":
    main()
