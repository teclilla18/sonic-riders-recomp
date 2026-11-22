"""Scripted Free Race at Metal City for balance measurements (docs/notes/super-balance.md).

py -3 tools/balance_route.py super|sonic|knuckles  -> prints the SR_INPUT value. Run it with
SR_ONLINE_SAVE=1 SR_ONLINE_RULES=1 (everything unlocked, deterministic clock, menus opened at the main menu)
and SR_BALLOG=1 (lap times, speed, rings). Same input for every character: the stick held forward from the
countdown (Metal City's walls guide a straight run round all three laps) and B held for 5 s every 10 s.
Menu timing (detclock steps, PC 2026-11-22): main menu 780; A 1600 Normal Race, 1720 Free Race, 1840 Track
Select, 2000 Metal City, 2150 the character; Player Entry's gear list from Sonic's Blue Star: Chaos Emerald is
18 presses LEFT (17 RIGHT). Knuckles: two RIGHT before the character's A. Screenshots shift menu timing: take
them only in the race.
"""
import sys


def route(who, t0=4300):
    s = []

    def press(t, b, d=10):
        s.extend([f"{t}:{b}", f"{t + d}:-{b}"])

    for t in (1600, 1720, 1840, 2000):
        press(t, "A")
    if who == "knuckles":
        press(2060, "RIGHT")
        press(2100, "RIGHT")
    press(2150, "A")
    for i in range(18):  # the same number of presses for everyone: Super ends on the Chaos Emerald, others go back
        press(2300 + i * 70, "LEFT" if who == "super" or i < 9 else "RIGHT")
    press(3600, "A")
    for t in range(3750, 4300, 120):
        press(t, "START")
    s.append(f"{t0}:SY=100")
    for k in range(24):
        t = t0 + 900 + k * 600
        s.extend([f"{t}:B", f"{t + 300}:-B"])
    s.sort(key=lambda x: int(x.split(":")[0]))
    return ",".join(s)


if __name__ == "__main__":
    print(route(sys.argv[1] if len(sys.argv) > 1 else "super"))
