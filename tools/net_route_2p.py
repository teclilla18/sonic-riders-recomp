"""2P race route for online tests (SR_INPUT scripts; docs/ONLINE.md "Phase 3").
python tools/net_route_2p.py p1|p2|p2local|both|online-p1|online-p2local|online-both [menu] -> prints the SR_INPUT value.
Player 1: recovered from the Switch's env.online.txt (2026-10-30 session).
Player 2: joins at Player Entry, picks a character, confirms; steers in the race."""
import sys

def press(t, b, port=""):
    return [f"{t}:{port}{b}", f"{t + 10}:-{port}{b}"]

def p1():
    s = []
    for t in (300, 600, 900, 1200):
        s += press(t, "START")
    for t in (1500, 1750, 2000, 2250, 2500, 2750, 3000, 3300, 3700, 4000, 4200):
        s += press(t, "A")
    s += press(4700, "START")
    s += steer(5900, "")
    return s

def steer(t0, port):
    s = [f"{t0}:{port}SY=100"]
    for t in range(t0 + 100, 8400, 120):
        s += [f"{t}:{port}SX=-60", f"{t + 50}:{port}SX=60", f"{t + 70}:{port}A", f"{t + 75}:-{port}A",
              f"{t + 100}:{port}SX=0"]
    return s

# player 2's menu presses: (frame, button); tuned by PC screenshots 2026-11-07 (the 2nd argument overrides: "3500:A,3600:RIGHT")
# player 2 joins at Player Entry (opened by player 1's A at 3700) and picks Knuckles and a board; it never presses
# on Track Select: the online rules (netplay.c online_rules, 2026-11-14) leave the menus to player 1
P2_MENU = [(3850, "A"), (3950, "RIGHT"), (4050, "RIGHT"), (4150, "A"), (4350, "A")]

def p2(menu=None, port="2."):
    """port "2." offline (SR_PADS=2); "" for the joining copy in a session (its own pad is port 1)"""
    s = []
    for t, b in (menu or P2_MENU):
        s += press(t, b, port)
    s += steer(5900, port)
    return s

def key(x):
    return int(x.split(":")[0])

# online sessions (and SR_ONLINE_RULES=1) open at Track Select by themselves (netplay.c online_rules, ~frame 940 on
# the online save); from there: player 1 picks Metal City, both pick a character and a board, player 1 presses START
def p1_online():
    s = []
    for t in range(1600, 2700, 120):  # Track Select: Metal City, then Player Entry: character and board (A repeated:
        s += press(t, "A")            # presses during a screen's animation are lost)
    for t in range(2800, 3400, 120):  # "Are you ready?": START (repeated for the same reason)
        s += press(t, "START")
    s += steer(4300, "")
    return s

def p2_online(port="2."):  # joins at Player Entry, keeps the first character (Tails) and board
    s = []
    for t in range(1800, 2700, 120):
        s += press(t, "A", port)
    s += steer(4300, port)
    return s

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    menu = None
    if len(sys.argv) > 2:
        menu = [(int(a.split(":")[0]), a.split(":")[1]) for a in sys.argv[2].split(",")]
    out = {"p1": p1(), "p2": p2(menu), "p2local": p2(menu, ""), "both": sorted(p1() + p2(menu), key=key),
           "online-p1": p1_online(), "online-p2local": p2_online(""),
           "online-both": sorted(p1_online() + p2_online(), key=key)}[which]
    print(",".join(out))
