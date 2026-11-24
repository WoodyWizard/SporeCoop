#!/usr/bin/env python3
"""Decision 8Q A: a galaxy server started with --encrypted-only refuses a game in clear (an older mod) and takes an
encrypted one. Starts build/galaxy_server(.exe) --local on a free port with an empty worlds folder.

    py tools/tests/galaxy_encrypted_only_check.py [path to galaxy_server(.exe)]
"""
import os
import socket
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import galaxy_server_check as g  # noqa: E402


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else g.EXE
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
    probe.close()
    worlds = tempfile.mkdtemp(prefix='galaxy_enc_only_')
    srv = subprocess.Popen([exe, '--local', '--worlds', worlds, '--encrypted-only', str(port), 'check'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        import time
        time.sleep(0.8)
        old = g.Player(port, 'Old Game', 0x0D1)
        old.send(g.HELLO, b'\x00' * 8)
        old.hello()
        ref = old.take(g.REFUSED, 1.0)
        g.check(ref is not None and b'encrypted games only' in ref and old.slot == 0,
                'a game in clear is refused, told it takes encrypted games only')
        e = g.EncPlayer(port, 'New Game', 0x0D2)
        g.check(e.exchange() and e.hello(), 'an encrypted game connects')
    finally:
        srv.kill()
    print('galaxy_encrypted_only_check: %s' % ('all passed' if not g.fails else '%d FAILED' % len(g.fails)))
    return 1 if g.fails else 0


if __name__ == '__main__':
    sys.exit(main())
