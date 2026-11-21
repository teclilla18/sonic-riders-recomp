"""Exercise the real minimized host's lobby and game handshake with synthetic UDP peers.

Usage: py -3 tools/test_online_protocol.py --exe build/host-mingw/sonicriders.exe --iso <own-disc.iso>
No game data leaves the machine. Captures/logs stay under ignored build/.
"""
import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--iso', required=True, type=Path)
    args = parser.parse_args()
    out = Path('build/online-protocol-test').resolve()
    (out / 'saves').mkdir(parents=True, exist_ok=True)
    (out / 'saves/settings.ini').write_text('update_check=0\npc_fps=0\npc_scale=1\n')
    env = dict(os.environ, SR_ONLINE='1', SR_LOBBY_TEST='host', SR_MINIMIZED='1', SR_LOGSYNC='1')
    env.pop('SR_NET', None)
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
            name = b'Protocol peer'
            join = lambda c: prefix + b'\x02' + struct.pack('<Q', c) + bytes([len(name)]) + name
            refuse = receive(lambda p: p.startswith(prefix + b'\x04'), join(compat + 1))
            assert b'Different online version' in refuse
            members = receive(lambda p: p.startswith(prefix + b'\x03'), join(compat))
            assert members[6] == 2 and name in members
            receive(lambda p: p.startswith(prefix + b'\x07'), prefix + b'\x05')
            # An incompatible number is refused even if a sender claims protocol 5.
            hello = bytearray(b'SR\x01\x05' + struct.pack('<f', 1.0) + b'different-platform-build'.ljust(64, b'\0') + struct.pack('<I', compat + 1))
            rejected = receive(lambda p: p.startswith(b'SR\x03'), hello, ('127.0.0.1', 7777))
            assert b'different online version' in rejected
            struct.pack_into('<I', hello, 72, compat)
            welcome = receive(lambda p: p.startswith(b'SR\x02'), hello, ('127.0.0.1', 7777))
            assert len(welcome) == 80 and struct.unpack_from('<I', welcome, 12)[0] == compat
            assert welcome[3] == 1 and welcome[5] == 2
            print('PASS: malformed packets, discovery, incompatible lobby/game rejection, different-build acceptance')
        finally:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == '__main__':
    main()
