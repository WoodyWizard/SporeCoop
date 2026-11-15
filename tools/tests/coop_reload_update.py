#!/usr/bin/env python3
"""A hot reload of coop.dll while its update check thread waits for the network (BUG-0060): the
thread used to return into the freed image of the old DLL (an access violation, or code of the new
image at the same base).

One game of the lane; a local server answers the update check only after 8 s. `coop.update check`
starts a check, `mod.reload coop` runs at once, then the server answers: the game must go on
(frames advance, the console answers, coop.update answers from the new image, no fault), and the
old thread must not run in the new image. Under Wine the new copy loads at the old base, so the
old thread returned into identical code of the new image and finished there with the new image's
WinHTTP state: the new image then logged two results, one of them an error (12019 / 6).

  SPORE_LANE=2 python3 tools/tests/coop_reload_update.py [--without-fix]
  (--without-fix: coop.fix core.update_pin off, the old thread without a module reference: must FAIL)"""
import http.server
import os
import sys
import threading
import time

import lane
from diag_check import GAME, logname
from sporectl import Console
from twoinst import Checks, alive, wait_up

HOLD = 8
PORT = 47990 + lane.LANE


class Slow(http.server.BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        time.sleep(HOLD)
        self.send_response(404)
        self.end_headers()

    def log_message(self, *a):
        pass


def main():
    without = '--without-fix' in sys.argv
    ck = Checks()
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), Slow)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    ini = os.path.join(GAME, 'SporebinEP1', 'mods', 'coop.ini')
    old = open(ini).read() if os.path.exists(ini) else None
    try:
        with open(ini, 'a') as f:
            f.write(f'\n[update]\ncheck=1\nurl=http://127.0.0.1:{PORT}/latest\n')
        lane.run_host()
        ck(wait_up(lane.HOST), 'the game starts')
        c = Console(lane.HOST)
        for _ in range(120):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 60 60000')
        time.sleep(HOLD + 2)  # the check of the start is over
        print(' ', c.call(f'coop.fix core.update_pin {"off" if without else "on"}')['out'].strip().replace('\n', ', '))
        print(' ', c.call('coop.update check')['out'].strip()[:120])
        time.sleep(0.5)
        print(' ', c.call('mod.reload coop')['out'].strip()[:200])
        time.sleep(HOLD + 6)  # the server answered the old image's thread
        ok = alive(lane.HOST)
        frames = ok and c.call('wait.frames 60 20000')['ok']
        ck(ok and frames, 'the game goes on after the old thread returned (console, frames)')
        upd = c.call('coop.update')['out'] if ok else ''
        print('  coop.update:', upd.strip()[:160])
        ck('update:' in upd, 'coop.update answers from the new image')
        # the old thread must not run on in the new image (same base, same code: it then finished
        # with the new image's state and handles; seen as a second "update check:" result there)
        time.sleep(2)
        log = open(logname(), errors='replace').read()
        tail = log[log.rfind('loaded coop'):]
        results = [l.split('coop: ', 1)[-1] for l in tail.splitlines() if 'update check:' in l]
        for r in results:
            print('  new image:', r)
        ck(len(results) == 1 and '404' in results[0],
           'the new image logs only its own check (the old thread stayed in the old image)')
        if ok:
            ck.faults(host=c)
    finally:
        if old is None:
            os.remove(ini)
        else:
            open(ini, 'w').write(old)
        srv.shutdown()
        lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
