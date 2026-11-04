#!/usr/bin/env python3
"""BUG-0511: Cell: after a level-up (the cell grows) the host's cells float around fast on the guest.

A level change rescales the game's world over ~3 s (WorldScaleStep: every position, velocity and size
times old / new world scale G+0x514C, about the origin), on each game at its own moment: the guest
levels up when the host's shared food total reaches it, a few frames later. Before the fix the guest
put the host's cell poses (sent in the host's scale) into its own world unchanged: while the two scales
differed the mirrors were up to 10 m off and glided there fast, the players' "cells float around".

Two games in the Cell fixture. The host's food goes up until its level changes (twice); while each
change runs (about 6 s) the test compares, every 0.3 s, each host cell with its mirror on the guest
(coop.census: the host's cell index on both games). Positions are compared in the guest's metres: a
game's position times its world scale (`world_scale=` of coop.pool, read around the dump) is the same
point on both games, divided by the guest's scale. Checks: both games reach the same level, and the
90th percentile of the mirror errors stays under 2 m in every sample (0.0-0.2 m before a level change).

  coop_cell_grow.py   (SPORE_FIX_OFF=cell.world_scale: errors of 4-10 m, it FAILS)"""
import math
import re
import time

import worlddiff as wd
from twoinst import Checks, start

LIMIT = 2.0


def pool(con):
    out = con.call('coop.pool')['out']
    lv = re.search(r'level=(-?\d+)', out)
    sc = re.search(r'world_scale=([\d.]+)', out)
    return (int(lv.group(1)) if lv else None), (float(sc.group(1)) if sc else None)


def cells(con, at=None):
    _, s0 = pool(con)
    w = wd.dump(con, 60, at)
    _, s1 = pool(con)
    s = (s0 + s1) / 2 if s0 and s1 else None
    return w, s, {r['id']: r['pos'] for r in w.rows if r['kind'] == 'l' and r['own'] in ('npc', 'proxy')}


def sample(host, guest, tag):
    gw, gs, gc = cells(guest)
    hw, hs, hc = cells(host, gw.me)
    common = [i for i in hc if i in gc]
    if not common or not hs or not gs:
        print(f'  {tag}: nothing to compare (host {len(hc)}, guest {len(gc)}, scales {hs} {gs})', flush=True)
        return None
    d = sorted(math.dist([v * hs / gs for v in hc[i][:2]], gc[i][:2]) for i in common)
    p90 = d[int(0.9 * (len(d) - 1))]
    print(f'  {tag}: {len(common)} cells, median {d[len(d) // 2]:.2f} m, p90 {p90:.2f} m, max {d[-1]:.2f} m '
          f'(scale host {hs:.3f} guest {gs:.3f})', flush=True)
    return p90


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(4)
    base = sample(host, guest, 'before')
    ck(base is not None and base < LIMIT, f'the mirrors are where the host\'s cells are before a level change ({base})')
    worst = []
    for k in range(2):
        l0, _ = pool(host)
        for _ in range(8):  # food until the level changes (a level every 50 points, the total is shared)
            host.call('coop.pool food 30')
            time.sleep(0.4)
            if pool(host)[0] != l0:
                break
        l1, _ = pool(host)
        ck(l1 is not None and l0 is not None and l1 > l0, f'level change {k + 1}: the host grew ({l0} -> {l1})')
        t0 = time.time()
        errs = []
        while time.time() - t0 < 6.5:
            e = sample(host, guest, f'level {l1} t+{time.time() - t0:.1f}')
            if e is not None:
                errs.append(e)
            time.sleep(0.3)
        worst.append(max(errs) if errs else None)
        gl, _ = pool(guest)
        ck(gl == l1, f'level change {k + 1}: the guest has the host\'s level ({gl}, host {l1})')
        time.sleep(3)
    print('  worst p90 per level change:', worst)
    ck(all(w is not None and w < LIMIT for w in worst),
       f'the mirrors stay with the host\'s cells while the world rescales (p90 under {LIMIT} m: {worst})')
    print('  guest:', [l for l in guest.call('coop.pool')['out'].splitlines() if 'world_scale' in l])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
