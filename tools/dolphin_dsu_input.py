# Scripted controller for Dolphin over the DSU (cemuhook) protocol: drives the original game in
# Dolphin to a given screen for reference frames (Dolphin ignores hand-made .dtm movies here).
# Setup (test Dolphin only): DSUClient.ini [Server] Enabled = True, Entries = Script:127.0.0.1:26760;
# GCPadNew.ini [GCPad1] Device = DSUClient/0/Script, Buttons/A = Cross, Buttons/Start = Options, ...
# Run Dolphin with -C Dolphin.Input.BackgroundInput=True.
# usage: python dolphin_dsu_input.py "<sec>:<BTN>[,<sec>:-<BTN>...]" [duration]
#   BTN: CROSS CIRCLE SQUARE TRIANGLE OPTIONS SHARE UP DOWN LEFT RIGHT L1 R1 (seconds from connect)
import socket, struct, sys, time, zlib

events = []
for tok in sys.argv[1].split(','):
    t, b = tok.split(':')
    events.append((float(t), b.lstrip('-'), not b.startswith('-')))
events.sort()
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 600

B1 = {'SHARE': 0x01, 'L3': 0x02, 'R3': 0x04, 'OPTIONS': 0x08, 'UP': 0x10, 'RIGHT': 0x20, 'DOWN': 0x40, 'LEFT': 0x80}
B2 = {'L2': 0x01, 'R2': 0x02, 'L1': 0x04, 'R1': 0x08, 'TRIANGLE': 0x10, 'CIRCLE': 0x20, 'CROSS': 0x40, 'SQUARE': 0x80}
SERVER_ID = 0x5352
MAC = b'\x00\x11\x22\x33\x44\x55'


def packet(msg_type, payload):
    body = struct.pack('<I', msg_type) + payload
    hdr = b'DSUS' + struct.pack('<HHII', 1001, len(body), 0, SERVER_ID)
    crc = zlib.crc32(hdr + body) & 0xFFFFFFFF
    return b'DSUS' + struct.pack('<HHII', 1001, len(body), crc, SERVER_ID) + body


def shared(slot, connected):
    return struct.pack('<BBBB', slot, 2 if connected else 0, 2 if connected else 0, 1 if connected else 0) + \
        (MAC if connected else b'\0' * 6) + struct.pack('<B', 5 if connected else 0)


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', 26760))
sock.settimeout(0.004)
client = None
start = None
held = set()
num = 0
next_ev = 0
last_send = 0
while True:
    try:
        data, addr = sock.recvfrom(1024)
        client = addr
        mtype = struct.unpack_from('<I', data, 16)[0]
        if mtype == 0x100000:
            sock.sendto(packet(0x100000, struct.pack('<H', 1001) + b'\0\0'), addr)
        elif mtype == 0x100001:
            n = struct.unpack_from('<i', data, 20)[0]
            for i in range(max(0, min(n, 4))):
                slot = data[24 + i]
                sock.sendto(packet(0x100001, shared(slot, slot == 0) + b'\0'), addr)
        if start is None and mtype == 0x100002:
            start = time.monotonic()
            print('client connected', addr, flush=True)
    except socket.timeout:
        pass
    if start is None:
        continue
    now = time.monotonic() - start
    if now > duration:
        break
    while next_ev < len(events) and events[next_ev][0] <= now:
        _, b, down = events[next_ev]
        (held.add if down else held.discard)(b)
        print(f'{now:7.2f} {"+" if down else "-"}{b}', flush=True)
        next_ev += 1
    if client and time.monotonic() - last_send > 0.008:
        last_send = time.monotonic()
        num += 1
        b1 = sum(v for k, v in B1.items() if k in held)
        b2 = sum(v for k, v in B2.items() if k in held)
        p = shared(0, True) + struct.pack('<BIBBBB', 1, num, b1, b2, 0, 0) + bytes([128, 128, 128, 128])
        an = ['LEFT', 'DOWN', 'RIGHT', 'UP', 'SQUARE', 'CROSS', 'CIRCLE', 'TRIANGLE', 'R1', 'L1', 'R2', 'L2']
        p += bytes(255 if k in held else 0 for k in an)  # analog d-pad, face buttons, shoulders
        p += bytes(12)  # two touches
        p += struct.pack('<Q', int(now * 1e6)) + struct.pack('<6f', 0, 1, 0, 0, 0, 0)
        sock.sendto(packet(0x100002, p), client)
