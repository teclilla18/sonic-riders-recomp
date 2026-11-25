"""Run a minimized PC Online Beta race and compare the peers' full recordings."""
import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import re
import struct
import subprocess
import time

from net_route_2p import p1_online, p2_online


def race(args):
    suffix = ("-mixed" if args.mixed else "") + ("-leave" if args.disconnect else "")
    root = args.out or Path("build/online-room-test") / f"{args.players}p-{args.mode}{suffix}"
    exe, iso = args.exe.resolve(), args.iso.resolve()
    common = {key: value for key, value in os.environ.items() if key not in ("SR_NET", "SR_ONLINE", "SR_LOBBY_TEST")}
    common.update(SR_WINDOW="1910x800", SR_MINIMIZED="1", SR_LOGSYNC="1",
                  SR_SCREEN="3", SR_SCREENSHOT="3900,4300,4900,5300",
                  SR_FRAMERATE="0", SR_FPS="0", SR_NET_PLAYERS=str(args.players))
    profiles = (("1910x800", "0", "0"), ("1068x600", "2", "0"),
                ("800x600", "0", "120"), ("960x600", "0", "45"))

    with ExitStack() as resources:
        processes, records, logs = [], [], []
        try:
            for slot in range(args.players):
                out = (root / f"p{slot + 1}").resolve()
                (out / "saves").mkdir(parents=True, exist_ok=True)
                (out / "build").mkdir(exist_ok=True)
                (out / "saves/settings.ini").write_text(f"online_name=TestPlayer{slot + 1}\npc_scale=1\nupdate_check=0\n")
                recording = out / "race.srr"
                recording.unlink(missing_ok=True)
                records.append(recording)
                route = p1_online() if slot == 0 else p2_online("")
                # Stagger steering so each guest contributes a distinguishable input stream.
                route = [entry for entry in route if int(entry.split(":")[0]) < 4300]
                route += [f"{4300 + 25 * slot}:SY=100", f"{4400 + 25 * slot}:SX={60 - 35 * slot}",
                          f"{4480 + 25 * slot}:SX=0", f"{4600 + 25 * slot}:A", f"{4640 + 25 * slot}:-A"]
                if args.finish:
                    route.extend(entry for entry in p1_online() if int(entry.split(":")[0]) >= 4800)
                env = dict(common, SR_INPUT=",".join(route), SR_RECORD=str(recording))
                if args.mixed:
                    window, framerate, fps = profiles[slot]
                    env.update(SR_SCREEN="3", SR_WINDOW=window, SR_FRAMERATE=framerate, SR_FPS=fps,
                               SR_SCREENSHOT="1900,2300,2900,3750,3900,4300,4900,5300,6300,8000",
                               SR_SHOTSTEP="4200,4850,5300")
                if args.mode == "lan":
                    env.update(SR_ONLINE="1", SR_LOBBY_TEST="host" if slot == 0 else "join:127.0.0.1")
                elif args.mode == "inet":
                    env.update(SR_ONLINE="1", SR_INET_TEST_IP="127.0.0.1",
                               SR_LOBBY_TEST="inet-host" if slot == 0 else "inet-join:2130706433")
                else:
                    env["SR_NET"] = f"{'join' if slot else 'host'}:127.0.0.1:{args.port}"
                if args.finish:
                    env["SR_SHOTSTEP"] = "2500,3700,5200,5300,12000,19400,20000,24500"
                logs.append(out / "sonicriders.log")
                log = resources.enter_context(logs[-1].open("wb"))
                processes.append(subprocess.Popen([str(exe), str(iso)], cwd=out, env=env, stdout=log, stderr=log))
                time.sleep(.45)
            width = 8 + 8 * args.players
            size = 64 + width * args.frames
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                codes = [proc.poll() for proc in processes]
                assert all(code is None for code in codes), f"Game exited early with {codes}. See {root}."
                if all(path.exists() and path.stat().st_size >= size for path in records):
                    break
                time.sleep(.5)
            else:
                raise AssertionError(f"Only recorded {[max(0, (p.stat().st_size - 64) // width) if p.exists() else 0 for p in records]} frames. See {root}.")
            data = [path.read_bytes()[:size] for path in records]
            for recording in data:
                assert recording[:8] == b"SRINPUT1"
                assert struct.unpack_from("<I", recording, 12)[0] == args.players
            for i in range(args.frames):
                offset = 64 + i * width
                expected = data[0][offset:offset + width]
                assert all(d[offset:offset + width] == expected for d in data[1:]), f"Recording differs at frame {i + 1}"
            if args.frames >= 4800:
                inputs = [set() for _ in records]
                for offset in range(64 + 4300 * width, 64 + 4800 * width, width):
                    for p in range(args.players):
                        inputs[p].add(data[0][offset + 10 + p * 8:offset + 12 + p * 8])
                assert all(len(sticks) > 1 for sticks in inputs), f"A port never steered. See {root}."
                assert len({frozenset(sticks) for sticks in inputs}) == args.players, "Steering streams should be distinct."
            print(f"PASS: {args.players} players over {args.mode}, {args.frames} matching input/checksum records per peer. {root}")
            if args.finish:
                finishes = []
                for path in logs:
                    pairs = re.findall(rb"\[net\] frame (\d+): player (\d+) crossed the finish line", path.read_bytes())
                    finishes.append({int(player): int(frame) for frame, player in pairs if 4300 < int(frame) <= args.frames})
                assert len(finishes[0]) == args.players and all(f == finishes[0] for f in finishes), finishes
                print(f"PASS: finish frames by seat {finishes[0]}")
            if args.disconnect:
                processes[-1].terminate()
                processes[-1].wait(timeout=10)
                deadline = time.monotonic() + 16
                while time.monotonic() < deadline:
                    if all(b"[net] session ended at frame" in path.read_bytes() for path in logs[:-1]):
                        break
                    time.sleep(.5)
                else:
                    raise AssertionError(f"Remaining players did not all leave lockstep. See {root}.")
                print(f"PASS: remaining {args.players - 1} peers ended the session after the last guest disconnected.")
        finally:
            for proc in processes:
                if proc.poll() is None:
                    proc.terminate()
            for proc in processes:
                proc.wait(timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--players", type=int, choices=(2, 3, 4), default=4)
    parser.add_argument("--mode", choices=("lan", "inet", "direct"), default="lan")
    parser.add_argument("--port", type=int, default=17888, help="port for direct connection tests")
    parser.add_argument("--frames", type=int, default=5400)
    parser.add_argument("--finish", action="store_true", help="require every racer to finish within --frames")
    parser.add_argument("--mixed", action="store_true", help="different window ratios and picture rates")
    parser.add_argument("--out", type=Path, help="separate output folder for a diagnostic build")
    parser.add_argument("--disconnect", action="store_true", help="end a guest after the recording check")
    parser.add_argument("--timeout", type=float, default=250)
    race(parser.parse_args())
