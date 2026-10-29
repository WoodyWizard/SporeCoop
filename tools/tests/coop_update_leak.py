#!/usr/bin/env python3
"""Every in-game update attempt leaked the decoded update pack (BUG-0241): the update threads left
through ExitThread / FreeLibraryAndExitThread with their locals in scope (BUG-0060), so the pack's
files (several MB) stayed allocated for the rest of the game, one more copy per Try again.

One game of the lane, no session. A local server offers a release that is newer than the running
one, but its pack calls itself the running version, so every attempt downloads and decodes it and
then stops with "the release pack is not newer" (nothing is installed). The pack holds one 20 MB
file. The committed private memory of the game (SporeKit `mem.commit`, a VirtualQuery walk) is
compared before and after 5 attempts.

  SPORE_LANE=N python3 tools/tests/coop_update_leak.py [--without-fix]
  (--without-fix or SPORE_FIX_OFF=core.update_unwind: the 0.7.3.0 threads, must FAIL)"""
import hashlib
import http.server
import json
import os
import re
import struct
import sys
import threading
import time

import lane
from diag_check import GAME
from sporectl import Console
from twoinst import Checks, alive, wait_up

PORT = 47970 + lane.LANE
PAD = 20 * 1024 * 1024
ATTEMPTS = 5


def make_pack(version):
    """a pack in the format of tools/mkpack.py: one big file at an allowed path"""
    data = os.urandom(PAD)
    path = b'SporebinEP1/mods/pad.bin'
    s16 = lambda b: struct.pack('<H', len(b)) + b  # noqa: E731
    head = b'SCUP' + struct.pack('<I', 1) + s16(version.encode()) + struct.pack('<I', 1)
    head += s16(path) + bytes([0]) + struct.pack('<I', len(data)) + hashlib.sha256(data).digest()
    return head + data


def serve(running):
    name = 'SporeCoop-99.0.0.pack'
    pack = make_pack(running)  # the same version inside: refused before apply_pack
    base = f'http://127.0.0.1:{PORT}'
    files = {'/download/' + name: pack,
             '/download/SHA256SUMS.txt': (hashlib.sha256(pack).hexdigest() + f'  {name}\n').encode()}
    files['/latest'] = json.dumps({
        'tag_name': 'v99.0.0', 'html_url': base + '/release',
        'assets': [{'name': n[10:], 'browser_download_url': base + n} for n in files if n.startswith('/download/')],
    }).encode()

    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            body = files.get(self.path)
            self.send_response(200 if body is not None else 404)
            self.send_header('Content-Length', str(len(body or b'')))
            self.end_headers()
            self.wfile.write(body or b'')

        def log_message(self, *a):
            pass

    srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, base + '/latest'


def running_version():
    src = open(os.path.join(lane.ROOT, 'mods', 'coop', 'coop.cpp'), errors='replace').read()
    return re.search(r'#define COOP_VERSION "([^"]+)"', src).group(1)


def commit_kb(c):
    m = re.search(r'commit_private_kb=(\d+)', c.call('mem.commit')['out'])
    return int(m.group(1)) if m else -1


def low(c, n=3):
    v = []
    for _ in range(n):
        v.append(commit_kb(c))
        time.sleep(2)
    return min(v)


def main():
    without = '--without-fix' in sys.argv or 'core.update_unwind' in os.environ.get('SPORE_FIX_OFF', '')
    ck = Checks()
    ver = running_version()
    srv, url = serve(ver)
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    mods = os.path.join(GAME, 'SporebinEP1', 'mods')
    ini = os.path.join(mods, 'coop.ini')
    pad = os.path.join(mods, 'pad.bin')
    old = open(ini, 'rb').read() if os.path.exists(ini) else None
    try:
        with open(ini, 'ab') as f:
            f.write(f'\r\n[update]\r\ncheck=1\r\nurl={url}\r\n'.encode())
        lane.run_host()
        ck(wait_up(lane.HOST), 'the game starts')
        c = Console(lane.HOST)
        for _ in range(120):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 60 60000')
        print(' ', c.call(f'coop.fix core.update_unwind {"off" if without else "on"}')['out'].strip()[:120])
        state = ''
        for _ in range(60):
            state = c.call('coop.update')['out']
            if 'available' in state:
                break
            time.sleep(1)
        print('  coop.update:', state.strip()[:160])
        ck('available' in state and 'latest=99.0.0' in state, 'the local server offers 99.0.0')
        before = low(c)
        print(f'  committed private memory before: {before // 1024} MB')
        refused = 0
        for i in range(ATTEMPTS):
            c.call('coop.update apply')
            out = ''
            for _ in range(60):
                time.sleep(1)
                out = c.call('coop.update')['out']
                if 'failed' in out or 'ready' in out:
                    break
            refused += 'the release pack is not newer' in out
            ck(not os.path.exists(pad), f'attempt {i + 1}: nothing installed ({out.strip()[:90]})')
        ck(refused == ATTEMPTS, f'every attempt stopped at "not newer" ({refused}/{ATTEMPTS})')
        time.sleep(2)
        after = low(c)
        grow = (after - before) // 1024
        print(f'  committed private memory after {ATTEMPTS} attempts: {after // 1024} MB (+{grow} MB)')
        ck(before > 0 and after > 0, 'mem.commit answers')
        ck(grow < 30, f'the packs were freed: +{grow} MB < 30 MB (a leak is ~{PAD * ATTEMPTS >> 20} MB)')
        ok = alive(lane.HOST)
        ck(ok and c.call('wait.frames 30 20000')['ok'], 'the game goes on (frames advance)')
        if ok:
            ck.faults(host=c)
    finally:
        if old is None:
            if os.path.exists(ini):
                os.remove(ini)
        else:
            open(ini, 'wb').write(old)
        if os.path.exists(pad):
            os.remove(pad)
        srv.shutdown()
        lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
