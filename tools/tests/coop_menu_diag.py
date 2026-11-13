#!/usr/bin/env python3
"""One game in the galaxy menu: the 0.7.14 P2 checks that need no partner (BUG-1025, BUG-1011, BUG-1015).

1. BUG-1025 (core.ui_release): 12 co-op notifications come and go; the game's own reference counts of the dropped
   panels and their labels (read while they are alive, `coop.notify`): every panel and label is freed (before: each
   label kept the mod's reference for good). Then 60 more panels: no fault, the game keeps running.
2. BUG-1011 (diag.summary_wide): the session summary file in a folder with Cyrillic letters (mods\\logs\\Тест, the
   test seam `coop.diag summarysub`): the file is there on disk and is read back (before: fopen took the UTF-8 path
   in the ANSI code page and wrote nothing).
3. BUG-1015 (core.update_busy): a local release server offers the next version with a broken pack.
   a) the check thread waits 3 s after its final state (`coop.update testdelay`: the old window between "available"
      and g_busy cleared, widened); Update (`coop.update apply`) in that window is taken: the download starts
      (before: the click was dropped, the state stayed "available");
   b) the download fails (the pack is not a pack); Try again with the download held 3 s before its first byte
      (`applydelay`): the co-op window's line starts at 0 % (before: the last attempt's 100 %).

SPORE_FIX_OFF=core.ui_release / diag.summary_wide / core.update_busy: that part FAILs.

  SPORE_LANE=4 python3 tools/tests/coop_menu_diag.py
"""
import hashlib
import http.server
import json
import os
import re
import shutil
import threading
import time

from twoinst import Checks, wait_up
from sporectl import Console
import lane

PORT = 47950  # + lane


def start():
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
    lane.run_host()
    assert wait_up(lane.HOST), 'the game did not start'
    c = Console(lane.HOST)
    for _ in range(180):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 300 60000')
    return c


def fix_off(c):
    names = [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]
    for n in names:
        print('  SPORE_FIX_OFF:', c.call(f'coop.fix {n} off')['out'].strip(), flush=True)


def notify_stats(c):
    m = re.search(r'notify dropped=(\d+) labels_freed=(\d+) labels_kept=(\d+) panel_refs=(-?\d+) label_refs=(-?\d+)',
                  c.call('coop.notify')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def serve(new):
    """a stand-in for the GitHub API: the next version, its "pack" is 300 KB that are not a pack"""
    junk = os.urandom(300 * 1024)
    name = f'SporeCoop-{new}.pack'
    base = f'http://127.0.0.1:{PORT + lane.LANE}'
    files = {f'/download/{name}': junk,
             '/download/SHA256SUMS.txt': f'{hashlib.sha256(junk).hexdigest()}  {name}\n'.encode()}
    files['/latest'] = json.dumps({'tag_name': 'v' + new, 'html_url': base + '/release',
                                   'assets': [{'name': n[10:], 'browser_download_url': base + n} for n in list(files)]
                                   }).encode()

    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = files.get(self.path)
            self.send_response(200 if body is not None else 404)
            self.send_header('Content-Length', str(len(body or b'')))
            self.end_headers()
            self.wfile.write(body or b'')

        def log_message(self, *a):
            pass

    srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT + lane.LANE), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, base + '/latest'


def ustate(c):
    out = c.call('coop.update')['out']
    m = re.match(r'update: (\w[\w ]*?) running=', out)
    line = re.search(r"line='([^']*)'", out)
    return (m.group(1) if m else '?'), (line.group(1) if line else ''), out.strip()


def wait(cond, secs, step=0.1):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return None


def main():
    c = start()
    fix_off(c)
    ck = Checks()

    # ---- 1. BUG-1025: the notification panels are freed ----
    for i in range(12):
        c.call(f'coop.notify test panel {i}')
        time.sleep(0.25)
    st = wait(lambda: (notify_stats(c) or (0,))[0] >= 12 and notify_stats(c), 20, 0.5)
    print('  notify:', c.call('coop.notify')['out'].splitlines()[1:2], flush=True)
    ck(st and st[3] == 1, f'BUG-1025: a dropped panel is held only by the main window (panel_refs {st and st[3]})')
    ck(st and st[0] >= 12 and st[2] == 0 and st[1] >= 12,
       f'BUG-1025: every label of the dropped panels is freed (dropped {st and st[0]}, labels freed {st and st[1]}, '
       f'kept {st and st[2]})')
    for i in range(60):
        c.call(f'coop.notify more {i}')
    time.sleep(10)
    st2 = notify_stats(c)
    ck(st2 and st2[0] >= st[0] + 60 if st else False, f'BUG-1025: 60 more panels built and dropped ({st2})')

    # ---- 2. BUG-1011: the summary file in a non-ASCII folder ----
    logs = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')
    sub = os.path.join(logs, 'Тест')
    shutil.rmtree(sub, ignore_errors=True)
    os.makedirs(sub)
    print(' ', c.call('coop.diag summarysub Тест')['out'].strip(), flush=True)
    out = c.call('coop.diag summarywrite')['out'].strip()
    c.call('coop.diag summarysub -')
    files = os.listdir(sub)
    print(f'  {out}; in {sub}: {files}', flush=True)
    ck(any(f.startswith('coop-last-session') for f in files) and 'read back 0 bytes' not in out,
       f'BUG-1011: coop-last-session.txt written into mods\\logs\\Тест and read back ({files}, {out!r})')
    shutil.rmtree(sub, ignore_errors=True)

    # ---- 3. BUG-1015: Update right after the check; Try again starts at 0 % ----
    running = re.search(r'running=(\S+)', c.call('coop.update')['out']).group(1)
    p = running.split('.')
    p[-1] = str(int(p[-1]) + 1)
    new = '.'.join(p)
    srv, url = serve(new)
    old_url = re.search(r'url=(\S+)', c.call('coop.update')['out']).group(1)
    try:
        print(' ', c.call(f'coop.update url {url}')['out'].strip(), c.call('coop.update testdelay 3000')['out'].strip(),
              flush=True)
        c.call('coop.update check')
        st = wait(lambda: ustate(c)[0] == 'available', 10, 0.05)
        ck(st is not None, f'BUG-1015: the check finds {new} ({ustate(c)[2]})')
        c.call('coop.update apply')  # within the 3 s the check thread still runs
        took = wait(lambda: ustate(c)[0] in ('downloading', 'failed', 'ready'), 2.5, 0.05)
        ck(took is not None, f'BUG-1015: Update right after the check is taken (state now: {ustate(c)[0]})')
        failed = wait(lambda: ustate(c)[0] == 'failed', 30, 0.2)
        if not failed:  # (switch off: the first click was dropped; a second one, as a player would)
            time.sleep(3.5)
            c.call('coop.update apply')
            failed = wait(lambda: ustate(c)[0] == 'failed', 30, 0.2)
        ck(failed is not None, f'the broken pack fails the update ({ustate(c)[2][-120:]})')
        c.call('coop.update testdelay 0')
        c.call('coop.update applydelay 3000')
        time.sleep(0.5)
        c.call('coop.update apply')  # Try again
        st = wait(lambda: ustate(c)[0] == 'downloading' and ustate(c), 2, 0.05)
        line = st[1] if st else ''
        print(f'  Try again: {line!r}', flush=True)
        ck(bool(st) and (line.endswith(' 0 %') or line.endswith(' 0 KB')),
           f'BUG-1015: Try again starts at 0 % ({line!r}; before: the last attempt\'s progress)')
        wait(lambda: ustate(c)[0] == 'failed', 30, 0.2)
    finally:
        c.call('coop.update applydelay 0')
        c.call('coop.update testdelay 0')
        c.call(f'coop.update url {old_url}')
        srv.shutdown()

    ck.faults(game=c)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
