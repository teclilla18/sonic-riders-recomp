"""Exercise the real minimized host's lobby and game handshake with synthetic UDP peers.

Usage: py -3 tools/test_online_protocol.py --exe build/host-mingw/sonicriders.exe --iso <own-disc.iso> [--inet]
--inet tests the internet lobby (one socket on UDP 7777, approval, session id, ping/delay, socket handoff).
No game data leaves the machine. Captures/logs stay under ignored build/.
"""
import argparse
from contextlib import ExitStack
import os
import re
from pathlib import Path
import socket
import struct
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--iso', required=True, type=Path)
    parser.add_argument('--inet', action='store_true')
    parser.add_argument('--capacity', action='store_true', help='fill an invite-code room and try a fifth player')
    args = parser.parse_args()
    if args.capacity:
        return capacity_test(args)
    if args.inet:
        return inet_test(args)
    out = Path('build/online-protocol-test').resolve()
    (out / 'saves').mkdir(parents=True, exist_ok=True)
    (out / 'saves/settings.ini').write_text('update_check=0\npc_fps=0\npc_scale=1\nscreen=3\n')
    env = dict(os.environ, SR_ONLINE='1', SR_LOBBY_TEST='host', SR_MINIMIZED='1', SR_LOGSYNC='1',
               SR_SCREEN='3', SR_WINDOW='955x400')
    env.pop('SR_NET', None)
    env['SR_NET_PLAYERS'] = '2'
    with (out / 'sonicriders.log').open('wb') as log, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        proc = subprocess.Popen([str(args.exe.resolve()), str(args.iso.resolve())], cwd=out, env=env, stdout=log, stderr=log)
        sock.bind(('127.0.0.1', 0))
        sock.settimeout(.25)
        host = ('127.0.0.1', 7778)
        prefix = b'SRLB\x01'

        def receive(test, send, address=host, timeout=15):
            until = time.monotonic() + timeout
            while time.monotonic() < until:
                if send:
                    sock.sendto(send, address)
                try:
                    packet, sender = sock.recvfrom(2048)
                except (socket.timeout, ConnectionResetError):
                    continue
                if test(packet):
                    return packet
            raise AssertionError('Timed out waiting for protocol response')

        try:
            # Malformed datagrams must not prevent discovery or joining.
            for bad in (b'', b'SRLB', b'SRLB\x01\x02', prefix + b'\x02' + b'\xff' * 8):
                sock.sendto(bad, host)
            announce = receive(lambda p: p.startswith(prefix + b'\x01'), prefix + b'\x08')
            compat = struct.unpack_from('<Q', announce, 6)[0]
            assert compat > 0
            assert announce[15] == 4
            name = b'Protocol peer'
            join = lambda c: prefix + b'\x02' + struct.pack('<Q', c) + bytes([len(name)]) + name
            refuse = receive(lambda p: p.startswith(prefix + b'\x04'), join(compat + 1))
            assert b'Different online version' in refuse
            members = receive(lambda p: p.startswith(prefix + b'\x03'), join(compat))
            assert members[6] == 2 and name in members
            receive(lambda p: p.startswith(prefix + b'\x07'), prefix + b'\x05')
            # An incompatible number is refused even if a sender claims protocol 6.
            hello = bytearray(b'SR\x01\x06' + struct.pack('<f', 1.0) + b'different-platform-build'.ljust(64, b'\0') + struct.pack('<I', compat + 1))
            rejected = receive(lambda p: p.startswith(b'SR\x03'), hello)
            assert b'different online version' in rejected
            struct.pack_into('<I', hello, 72, compat)
            welcome = receive(lambda p: p.startswith(b'SR\x02'), hello)
            assert len(welcome) == 80 and struct.unpack_from('<I', welcome, 12)[0] == compat
            assert welcome[3] == 1 and welcome[5] == 2
            assert abs(struct.unpack_from('<f', welcome, 8)[0] - 8 / 3) < 1e-6
            with (out / 'sonicriders.log').open(errors='replace') as host_log:
                assert any("camera aspect fixed for this run: 3.5556" in line and "this screen draws 2.3875" in line
                           for line in host_log)
            print('PASS: malformed packets, discovery, version checks, different-build acceptance, local room picture')
        finally:
            proc.terminate()
            proc.wait(timeout=10)


def inet_test(args):
    out = Path('build/online-inet-test').resolve()
    (out / 'saves').mkdir(parents=True, exist_ok=True)
    (out / 'saves/settings.ini').write_text('update_check=0\npc_fps=0\npc_scale=1\n')
    env = dict(os.environ, SR_ONLINE='1', SR_LOBBY_TEST='inet-host', SR_MINIMIZED='1', SR_LOGSYNC='1', SR_NET_PLAYERS='2',
               SR_INET_TEST_IP='127.0.0.1')
    env.pop('SR_NET', None)
    assert int.from_bytes(socket.inet_aton('127.0.0.1'), 'big') == 2130706433  # its 10-digit code
    prefix = b'SRLB\x01'
    host = ('127.0.0.1', 7777)
    with (out / 'sonicriders.log').open('wb') as log, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as me, \
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as other:
        proc = subprocess.Popen([str(args.exe.resolve()), str(args.iso.resolve())], cwd=out, env=env, stdout=log, stderr=log)
        for s in (me, other):
            s.bind(('127.0.0.1', 0))
            s.settimeout(.1)
        sid = [None]

        def pump(sock, send, test, timeout=15, every=.25, address=host):
            until, last = time.monotonic() + timeout, 0
            while time.monotonic() < until:
                if send and time.monotonic() - last > every:
                    sock.sendto(send() if callable(send) else send, address)
                    last = time.monotonic()
                try:
                    packet, _ = sock.recvfrom(2048)
                except (socket.timeout, ConnectionResetError):
                    continue
                if sid[0] and packet.startswith(prefix + b'\x05') and len(packet) == 22:  # answer the host's pings
                    sock.sendto(prefix + b'\x0a' + packet[6:], address)
                if test(packet):
                    return packet
            return None

        try:
            time.sleep(4)  # the test hook waits 2 s, then the code takes up to 6 s; packets before that are dropped
            for bad in (b'', b'SRLB', b'SRLB\x01\x02', prefix + b'\x02' + b'\xff' * 8,
                        b'\x01\x01\x00\x00\x21\x12\xa4\x42' + b'\x00' * 12,               # STUN reply, unknown id
                        b'\x01\x01\x00\x0c\x21\x12\xa4\x42' + b'\x00' * 12 + b'\x00\x20\x00\x08'):  # bad length
                me.sendto(bad, host)
            name = b'Internet peer'
            join = lambda c: prefix + b'\x02' + struct.pack('<Q', c) + bytes([len(name)]) + name
            refuse = pump(me, join(0), lambda p: p.startswith(prefix + b'\x04'), timeout=12)
            assert refuse and b'Different online version' in refuse, refuse
            compat = int(re.search(r'NET_COMPAT (\d+)', Path('runtime/src/net_compat.h').read_text()).group(1))
            d = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); d.bind(('127.0.0.1', 0)); d.settimeout(.2)
            ann = pump(d, prefix + b'', lambda p: p.startswith(prefix + b''), timeout=3, address=('127.0.0.1', 7778))
            d.close()
            assert ann is None, 'an internet host must not answer LAN discovery'
            wait = pump(me, join(compat), lambda p: p.startswith(prefix + b'\x0b') or p.startswith(prefix + b'\x03'))
            assert wait, 'no WAIT/LIST from the internet host'
            members = pump(me, join(compat), lambda p: p.startswith(prefix + b'\x03'))
            assert members and members[6] == 2 and name in members, members
            sid[0] = members[-8:]
            bad_ping = prefix + b'\x05' + b'\x00' * 8 + struct.pack('<Q', 1)
            assert pump(me, bad_ping, lambda p: p.startswith(prefix + b'\x0a'), timeout=1.5) is None, 'PONG to a wrong session id'
            ping = lambda: prefix + b'\x05' + sid[0] + struct.pack('<Q', 7)
            pong = pump(me, ping, lambda p: p.startswith(prefix + b'\x0a'), timeout=5)
            assert pong and pong[6:14] == sid[0], pong
            start = pump(me, ping, lambda p: p.startswith(prefix + b'\x07'), timeout=20)
            assert start and len(start) == 15 and start[6:14] == sid[0], start
            delay = start[14]
            assert 2 <= delay <= 15, delay
            hello = bytearray(b'SR\x01\x06' + struct.pack('<f', 1.0) + b'inet-peer'.ljust(64, b'\0') + struct.pack('<I', compat & 0xffffffff))
            other.settimeout(.1)
            assert pump(other, bytes(hello), lambda p: p.startswith(b'SR\x02'), timeout=3) is None, 'game answered a stranger'
            welcome = pump(me, bytes(hello), lambda p: p.startswith(b'SR\x02'), timeout=20)
            assert welcome and len(welcome) == 80 and welcome[4] == delay, welcome
            print('PASS: inet lobby: malformed/STUN-like packets, version refusal, approval, session id, '
                  'ping/pong, START delay %d, game handshake on the handed-over socket, stranger ignored' % delay)
        finally:
            proc.terminate()
            proc.wait(timeout=10)


def capacity_test(args):
    out = Path('build/online-capacity-test').resolve()
    (out / 'saves').mkdir(parents=True, exist_ok=True)
    (out / 'saves/settings.ini').write_text('update_check=0\npc_fps=0\npc_scale=1\n')
    compat = int(re.search(r'NET_COMPAT (\d+)', Path('runtime/src/net_compat.h').read_text()).group(1))
    env = dict(os.environ, SR_ONLINE='1', SR_LOBBY_TEST='inet-host', SR_MINIMIZED='1',
               SR_LOGSYNC='1', SR_NET_PLAYERS='4', SR_INET_TEST_IP='127.0.0.1')
    env.pop('SR_NET', None)
    host = ('127.0.0.1', 7777)
    prefix = b'SRLB\x01'
    with ExitStack() as stack:
        peers = [stack.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) for _ in range(4)]
        log = stack.enter_context((out / 'sonicriders.log').open('wb'))
        proc = subprocess.Popen([str(args.exe.resolve()), str(args.iso.resolve())], cwd=out, env=env, stdout=log, stderr=log)
        try:
            for slot, peer in enumerate(peers, start=2):
                peer.bind(('127.0.0.1', 0))
                peer.settimeout(.15)
                name = f'Guest{slot}'.encode()
                join = prefix + b'\x02' + struct.pack('<Q', compat) + bytes([len(name)]) + name
                now = time.monotonic()
                deadline = now + (5 if slot == 2 else 1.4)
                while time.monotonic() < deadline:
                    peer.sendto(join, host)
                    try:
                        packet, _ = peer.recvfrom(2048)
                    except (socket.timeout, ConnectionResetError):
                        continue
                    if slot <= 4 and packet.startswith(prefix + b'\x03') and packet[6] == slot and name in packet:
                        break
                    if slot == 5 and packet.startswith(prefix + b'\x04') and b'full' in packet:
                        break
                else:
                    raise AssertionError(f'Unexpected response for guest {slot}')
            print('PASS: four players admitted and fifth refused in the invite-code room')
        finally:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == '__main__':
    main()
