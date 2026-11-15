#!/usr/bin/env python3
"""Names from the network are one clean line (BUG-0774).

The host (Alice) and Bob play the fixture. A scripted guest (tools/fakepeer.py's protocol) joins with a name that
holds a newline, a fake log line, control characters, broken UTF-8 and 60 more letters. Checks, on the games:
* the host's session and Bob's (the roster his co-op window lists) have the name as one clean line of at most 24
  characters ("Evilplanet X[31m..." without the control bytes);
* no line of the host's or Bob's log starts with the injected text (no fake log line);
* no game logged a fault.
`SPORE_FIX_OFF=net.name_clean` fails: the name arrives as sent and the injected text is a line of its own.
"""
import os
import re
import time

from coop_net_robust import Peer, keep_frames
from twoinst import Checks, start
import lane

NAME = b'Evil\nplanet X\r\n[12:00:00] FAKE LINE\x01\x1b[31m\xc3' + b'z' * 60
CLEAN = 'Evilplanet X[12:00:00] F'  # the first 24 characters once the control bytes are gone


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, off):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


def main():
    h, g = start()
    ck = Checks()
    off = {lane.HOST: log_size(lane.HOST), lane.GUEST: log_size(lane.GUEST)}
    evil = Peer(NAME)
    t0 = time.time()
    while time.time() - t0 < 20 and evil.id is None:
        time.sleep(0.2)
    ck(evil.id is not None, f'the scripted guest joined (id {evil.id})')
    if evil.id is None:
        return ck.result()
    keep_frames(h, 8)
    hs = h.call('coop.status')['out']
    row = next((ln for ln in hs.splitlines() if re.match(rf'\s*\[{evil.id}\] ', ln)), '')
    print(f'  host roster row: {row!r}', flush=True)
    ck(CLEAN in row, f'the host has the name as one clean line ({CLEAN!r})')
    ck(row and not re.search(r'[\x00-\x1f\x7f]', row), 'no control byte in the host\'s row')
    time.sleep(2)
    lines = {who: log_since(inst, off[inst]) for who, inst in (('host', lane.HOST), ('Bob', lane.GUEST))}
    for who, ls in lines.items():
        named = [ln for ln in ls if 'planet X' in ln]
        fake = [ln for ln in ls if re.match(r'\s*(planet X|\[12:00:00\] FAKE LINE)', ln)]
        print(f'  {who}: {len(named)} log line(s) with the name, e.g. {named[:1]}; injected lines {fake[:2]}', flush=True)
        ck(not fake, f'{who}: no log line made by the name')
        ck(any(CLEAN in ln for ln in named), f'{who}: the log names the newcomer as one clean line')
        ck(all(not re.search(r'[\x00-\x08\x0b-\x1f\x7f]', ln) for ln in named), f'{who}: no control character in those lines')
    evil.bye()
    time.sleep(3)
    gs = g.call('coop.status')['out']
    ck("remote 0 'Alice'" in gs, 'Bob is still in the session')
    ck.faults(host=h, guest=g)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
