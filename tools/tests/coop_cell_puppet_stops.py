#!/usr/bin/env python3
"""Cell: the partner's cell does not swim on past a stop in a latency spike (BUG-0887).

A partner's cell (its puppet) is drawn at the pose its state buffer gives for now - 0.1 s; past the newest
state (a state later than that: a latency spike) the buffer moved it on along the last velocity for up to
0.25 s. In Creature that extrapolation falls off quickly (drive.puppet_fall, BUG-0680); in Cell it did not,
and modes::cell_drive puts the cell exactly at the sampled pose: a cell that stopped in a spike was drawn
swimming on, then stepping back.

Run over the player network (SPORE_NET=player: jitter and latency spikes). The guest's cell swims short
legs (`coop.pool goto dx dy`, as a click) and stops; both games record what they draw (`coop.bodyrec`, the
game's positions of every synchronized body with one clock). Per stop of the guest's own cell (swimming
over 1.5 m/s, then under 0.3 m/s within 0.6 s and staying so 0.5 s): how far the HOST's puppet of it was ahead of
the guest's own cell at the same moment, along the way it swam, in the 1.5 s after the stop (drawn 0.1 s late it
is never ahead; drawn swimming on in a spike it is). Checks: at least 10 stops, and none drawn
past by more than PAST m.
SPORE_FIX_OFF=drive.cell_fall must FAIL (stops drawn past in the spikes).

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


def stops(walker_rows, observer_rows, key):
    """[(past m, t, speed m/s)] per stop of the walker's own body `key` (it swam over 1.5 m/s, then under 0.3 m/s within
    0.6 s and stayed so for 0.5 s): the largest lead of the observer's copy over the walker's own body AT THE SAME
    MOMENT along the way it swam, in the 1.5 s from the slowing down (the copy shown 0.1 s late leads by nothing)"""
    rv, cv = by_key(walker_rows).get(key, []), by_key(observer_rows).get(key, [])
    rt, ct = [x[0] for x in rv], [x[0] for x in cv]
    out, k = [], 1
    while k < len(rv) - 2:
        sp, i0 = speed_at(rv, k)
        if 1.5 < sp < 20:
            d = ((rv[k][1][0] - rv[i0][1][0]) / (sp * (rv[k][0] - rv[i0][0])), (rv[k][1][1] - rv[i0][1][1]) / (sp * (rv[k][0] - rv[i0][0])))
            j = k
            while j < len(rv) - 1 and rv[j][0] < rv[k][0] + 0.6 and speed_at(rv, j)[0] > 0.3:
                j += 1
            if j < len(rv) - 1 and speed_at(rv, j)[0] <= 0.3:
                ts = rv[j][0]
                e = bisect.bisect_left(rt, ts + 0.5)
                if e < len(rv) and all(speed_at(rv, q)[0] < 0.5 for q in range(j, e)):
                    lead = []
                    for q in range(bisect.bisect_left(ct, ts - 0.3), bisect.bisect_right(ct, ts + 1.5)):
                        w = at_time(rv, rt, cv[q][0])
                        if w:
                            c = cv[q][1]
                            lead.append((c[0] - w[0]) * d[0] + (c[1] - w[1]) * d[1])
                    if len(lead) > 10:
                        out.append((round(max(lead), 3), round(ts, 2), round(sp, 2)))
                    k = e
                    continue
        k += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--legs', type=int, default=40)
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
    for c in (host, guest):
        body_trail.record(c, True)
    for i in range(a.legs):
        ang = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(4, 9)
        guest.call(f'coop.pool goto {dist * math.cos(ang):.1f} {dist * math.sin(ang):.1f}')
        time.sleep(rng.uniform(0.8, 1.4))
        guest.call('coop.pool goto 0 0')  # stop here (the game's own slowing down)
        time.sleep(rng.uniform(1.3, 1.8))
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
    st = stops(wr, orr, key)
    pasts = sorted(x[0] for x in st)
    print(f'  {len(st)} stops of the guest\'s cell ({key}); drawn past the stop in the host\'s game: p50 '
          f'{body_trail.pct(pasts, .5):.2f} p90 {body_trail.pct(pasts, .9):.2f} max {max(pasts, default=0):.2f} m', flush=True)
    for x in sorted(st, reverse=True)[:6]:
        print(f'    past {x[0]:.2f} m at t {x[1] - (wr[0][0] if wr else 0):.1f} s, swimming at {x[2]:.1f} m/s', flush=True)
    ck(len(st) >= 10, f'enough stops to judge ({len(st)} >= 10)')
    bad = [x for x in st if x[0] > PAST]
    ck(not bad, f'the host\'s game never draws the guest\'s cell past where it stopped (over {PAST} m after {len(bad)} of {len(st)} stops)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
