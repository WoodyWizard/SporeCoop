#!/usr/bin/env python3
"""BUG-0606: Cell: the sync-check found the host's cell hundreds of metres apart on every guest at the
end of the Cell stage.

In lab 20261105-233139 campaign_ct#1 the campaign's shortcut `coop.pool food 1000` (the food bar full)
started the end of Cell on the host: its cinematic, then the world scale shrinking for ~40 s until
History. The world scale step multiplies every position by old / new scale about the origin
(docs/re/cell.md#cell-world-scale): the host's cell, 74 m from the origin, went to 42 000 "metres"
at ~2800 m/s in the game's numbers while it stood still on the screen, and every guest's sync-check
compared the host's raw position with the puppet it drew 0.1-0.3 s earlier: p0 359-740 apart, a
MISMATCH the lab fails on, with no difference a player could see.

Two games in the Cell fixture, the guest 4 m from the host. The host's food goes to 1000; for 30 s
(the cinematic and the changing scale) every 0.5 s both games print their world scale, their own
cell and the partner's puppet in one frame (`coop.pool scalepos`), and the host asks a sync-check
every second. Checks: what the guest's game shows (the host's puppet against the host's own cell, both
in the guest's metres: positions times their game's world scale, divided by the guest's) stays within
3 m in 90 % of the samples, and the host's sync-check (asked every second, every difference logged)
reports no position apart and no MISMATCH (at least 3 checks done). Cells the level change removes
and adds may differ for one check (extra / missing, rechecked 3 s later): not this bug.

  coop_cell_end_sync.py   (SPORE_FIX_OFF=diag.cell_scale: p apart while the scales differ, it FAILS)"""
import math
import os

import lane
import re
import time

from twoinst import Checks, start

LIMIT = 3.0


def scalepos(con):
    out = con.call('coop.pool scalepos')['out']
    sc = re.search(r'scale=([\d.e+-]+)', out)
    me = re.search(r'me=\((-?[\d.]+) (-?[\d.]+)\)', out)
    pp = {int(i): (float(x), float(y)) for i, x, y in re.findall(r'p(\d)=\((-?[\d.]+) (-?[\d.]+)\)', out)}
    if not sc or not me:
        return None
    return float(sc.group(1)), (float(me.group(1)), float(me.group(2))), pp


def counts(con):
    m = re.search(r'checks (\d+) ok (\d+) differs (\d+) mismatch (\d+)', con.call('coop.diag')['out'] or '')
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0, 0)


def playing(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return m and int(m.group(1)) == 0 and 'cine=0' in con.call('coop.pool')['out']


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def p90(v):
    v = sorted(v)
    return v[int(0.9 * (len(v) - 1))] if v else None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    guest.call('coop.pool tp 4 0')
    time.sleep(3)
    host.call('coop.diag detail 2')  # every difference in the log
    c0 = counts(host)
    try:
        log0 = os.path.getsize(lane.game_log(lane.HOST))
    except OSError:
        log0 = None
    print('  host:', host.call('coop.pool food 1000')['out'].strip()[:80], flush=True)
    t0, last_ask, errs = time.time(), 0.0, []
    while time.time() - t0 < 30:
        h, g = scalepos(host), scalepos(guest)
        if h and g and 0 in g[2] and h[0] > 0 and g[0] > 0:
            hs, hme, _ = h
            gs, _, gp = g
            e = math.dist([v * gs for v in gp[0]], [v * hs for v in hme]) / gs
            errs.append(e)
            print(f'  t+{time.time() - t0:4.1f}: host scale {hs:.5f} pos ({hme[0]:.1f} {hme[1]:.1f}) | guest scale {gs:.5f} '
                  f'draws the host at ({gp[0][0]:.1f} {gp[0][1]:.1f}): {e:.2f} guest m off', flush=True)
        if time.time() - last_ask >= 1.0:
            host.call('coop.diag sync now')
            last_ask = time.time()
        time.sleep(0.5)
    time.sleep(4.5)  # a recheck that was due
    c1 = counts(host)
    d = [b - a for a, b in zip(c0, c1)]
    print(f'  sync-check on the host: checks +{d[0]} ok +{d[1]} differs +{d[2]} mismatch +{d[3]}', flush=True)
    pe = p90(errs)
    ck(pe is not None and pe < LIMIT, f'the guest draws the host\'s cell where it is through the end of Cell (p90 {pe} guest m, {len(errs)} samples)')
    ck(d[0] >= 3, f'the host checked the guest at least 3 times ({d[0]})')
    apart = []
    if log0 is not None:
        with open(lane.game_log(lane.HOST), 'rb') as f:
            f.seek(log0)
            apart = [l for l in f.read().decode('utf-8', 'replace').splitlines()
                     if 'sync-check:' in l and ('differs' in l or 'MISMATCH' in l) and ' apart (' in l]
    for l in apart[:5]:
        print('  ' + l[:220], flush=True)
    ck(log0 is not None, 'the host\'s game log is readable')
    ck(not apart, f'the sync-check finds no position apart where the games agree ({len(apart)} checks)')
    ck(d[3] == 0, f'no sync-check MISMATCH ({d[3]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
