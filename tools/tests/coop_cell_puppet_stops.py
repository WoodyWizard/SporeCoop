#!/usr/bin/env python3
"""Cell: the partner's cell does not swim on past a stop in a latency spike (BUG-0887).

A partner's cell (its puppet) is drawn at the pose its state buffer gives for now - 0.1 s; past the newest
state (a state later than that: a latency spike) the buffer moved it on along the last velocity for up to
0.25 s. In Creature that extrapolation falls off quickly (drive.puppet_fall, BUG-0680); in Cell it did not,
and modes::cell_drive puts the cell exactly at the sampled pose: a cell that stopped in a spike was drawn
swimming on, then stepping back.

Run over the player network (SPORE_NET=player: jitter, and a 250 ms latency spike every 7 s). The guest's cell
swims back and forth (`coop.pool goto dx dy`, as clicks: a stop and a turn every leg, the sharpest change of a
cell's way); both games record the players' cells as they draw them (`coop.bodyrec start kinds p`, one clock).
For every row of the HOST's copy of the guest's cell: how far it is ahead of the guest's own cell at the same
moment, along the way that cell swam 0.25 s before (drawn 0.1 s late it is behind; moved on along an old velocity
through a spike while the cell turned, it is ahead). Checks: over 1000 rows, none ahead by more than PAST m.
Not reproduced (2026-11-12): the code without a fall-off in Cell never drew the copy ahead (max 0.03 m in 1351 rows,
lab 20261112-065254; with Creature's fall-off in Cell 0.01 m, 20261112-065045): the switch tried (drive.cell_fall) was
taken out again. Kept as a regression test.

  SPORE_NET=player SPORE_LANE=3 python3 tools/tests/coop_cell_puppet_stops.py [--legs 40]
"""
import argparse
import bisect
import collections
import json
import os
import math
import random
import re
import time

import body_trail
import lane
from coop_cell_puppet_scale import playing, wait
from twoinst import Checks, start

PAST = 0.35  # m


def by_key(rows):
    by = collections.defaultdict(list)
    for t, k, p in rows:
        by[k].append((t, p[:2]))
    for v in by.values():
        v.sort()
    return by


def speed_at(v, k, back=0.2):
    """speed of rows v around index k over the `back` s before it"""
    t, p = v[k]
    i = k
    while i > 0 and v[i - 1][0] > t - back:
        i -= 1
    return math.dist(v[i][1], p) / (t - v[i][0]) if t > v[i][0] else 0.0, i


def at_time(v, ts, t):
    """position of rows v (times ts) at t, interpolated; None outside"""
    j = bisect.bisect_left(ts, t)
    if j <= 0 or j >= len(v):
        return None
    (t0, a), (t1, b) = v[j - 1], v[j]
    if t1 - t0 > 0.25:
        return None
    f = (t - t0) / (t1 - t0) if t1 > t0 else 0
    return (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f)


def vel_at(v, ts, t, win=0.1):
    a, b = at_time(v, ts, t - win), at_time(v, ts, t)
    return ((b[0] - a[0]) / win, (b[1] - a[1]) / win) if a and b else None


def leads(walker_rows, observer_rows, key):
    """per row of the observer's copy: how far it is AHEAD of the walker's own body at the same moment along the way
    the walker swam 0.25 s before (a copy drawn 0.1 s late is behind; one moved on along an old velocity through a
    latency spike while the walker stopped or turned is ahead): [(lead m, t, speed then m/s)]"""
    rv, cv = by_key(walker_rows).get(key, []), by_key(observer_rows).get(key, [])
    rt = [x[0] for x in rv]
    out = []
    for t, c in cv:
        w, v = at_time(rv, rt, t), vel_at(rv, rt, t - 0.25)
        if not w or not v:
            continue
        sp = math.hypot(*v)
        if not 1.0 < sp < 20:  # (the tp back next to the host: a jump)
            continue
        out.append(((c[0] - w[0]) * v[0] / sp + (c[1] - w[1]) * v[1] / sp, t, sp))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--legs', type=int, default=30)
    ap.add_argument('--seed', type=int, default=7)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    guest.call('coop.pool tp 4 0')
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    time.sleep(2)
    for c in (host, guest):  # the players' cells only (the pool's mirrors are hundreds of rows a frame)
        c.call('coop.bodyrec start kinds p')
    for i in range(a.legs):  # back and forth: a stop and a turn every leg (the sharpest change of a cell's way)
        ang = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(5, 9)
        dx, dy = dist * math.cos(ang), dist * math.sin(ang)
        guest.call(f'coop.pool goto {dx:.1f} {dy:.1f}')
        time.sleep(rng.uniform(0.9, 1.4))
        guest.call(f'coop.pool goto {-dx:.1f} {-dy:.1f}')
        time.sleep(rng.uniform(0.9, 1.4))
        if i % 8 == 7:
            guest.call('coop.pool tp 4 0')  # back next to the host (its screen, its tiles)
            time.sleep(1.5)
    for c in (host, guest):
        body_trail.record(c, False)
    wr, orr = body_trail.dump(guest), body_trail.dump(host)
    key = f'p{gid}'
    path = os.path.join(lane.out_dir('track'), f'cell_puppet_stops_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'walker': wr, 'observer': orr, 'key': key}, f)
    print(f'  rows: {path}', flush=True)
    rv = by_key(wr).get(key, [])
    sp = sorted(speed_at(rv, k)[0] for k in range(1, len(rv)))
    print(f'  the guest\'s own cell: {len(rv)} rows, speed p10 {body_trail.pct(sp, .1):.2f} p50 {body_trail.pct(sp, .5):.2f} '
          f'p90 {body_trail.pct(sp, .9):.2f} m/s', flush=True)
    ld = leads(wr, orr, key)
    xs = sorted(x[0] for x in ld)
    print(f'  {len(ld)} rows of the host\'s copy while the guest\'s cell swam: ahead of it p50 {body_trail.pct(xs, .5):.2f} '
          f'p99 {body_trail.pct(xs, .99):.2f} max {max(xs, default=0):.2f} m', flush=True)
    t0 = wr[0][0] if wr else 0
    for x in sorted(ld, reverse=True)[:6]:
        print(f'    {x[0]:.2f} m ahead at t {x[1] - t0:.1f} s (it swam {x[2]:.1f} m/s 0.25 s before)', flush=True)
    ck(len(ld) > 1000, f'enough rows to judge ({len(ld)} > 1000)')
    bad = [x for x in ld if x[0] > PAST]
    ck(not bad, f'the host\'s game never draws the guest\'s cell ahead of where it is (over {PAST} m in {len(bad)} of {len(ld)} rows)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
