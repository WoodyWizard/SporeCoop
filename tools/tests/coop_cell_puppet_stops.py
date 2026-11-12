#!/usr/bin/env python3
"""Cell: the partner's cell does not swim on past a stop in a latency spike (BUG-0887).

A partner's cell (its puppet) is drawn at the pose its state buffer gives for now - 0.1 s; past the newest
state (a state later than that: a latency spike) the buffer moved it on along the last velocity for up to
0.25 s. In Creature that extrapolation falls off quickly (drive.puppet_fall, BUG-0680); in Cell it did not,
and modes::cell_drive puts the cell exactly at the sampled pose: a cell that stopped in a spike was drawn
swimming on, then stepping back.

Run over the player network (SPORE_NET=player: jitter and latency spikes). The guest's cell swims short
legs (`coop.pool goto dx dy`, as a click) and stops; both games record what they draw (`coop.bodyrec`, the
game's positions of every synchronized body with one clock). Per stop of the guest's own cell (moving
> 1.5 m/s over 0.3 s, then within 0.4 m for the next second): how far the HOST's puppet of it went past the
stop point along the way it swam, in the 1.5 s after the stop. Checks: at least 10 stops, and none drawn
past by more than PAST m.
SPORE_FIX_OFF=drive.cell_fall must FAIL (stops drawn past in the spikes).

  SPORE_NET=player SPORE_LANE=3 python3 tools/tests/coop_cell_puppet_stops.py [--legs 40]
"""
import argparse
import bisect
import collections
import math
import random
import re
import time

import body_trail
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


def stops(walker_rows, observer_rows, key):
    """[(past m, t, way m/s)] per stop of the walker's own body `key`"""
    rv, cv = by_key(walker_rows).get(key, []), by_key(observer_rows).get(key, [])
    ct = [x[0] for x in cv]
    out, k = [], 0
    while k < len(rv) - 2:
        t, p = rv[k]
        prev = [x for x in rv[max(0, k - 80):k] if x[0] > t - 0.3]
        post = [x for x in rv[k:k + 500] if x[0] < t + 1.0]
        if (len(prev) >= 3 and math.dist(prev[0][1], p) / (t - prev[0][0]) > 1.5 and len(post) > 5 and
                post[-1][0] - t > 0.8 and max(math.dist(p, q) for _, q in post) < 0.4):
            v = ((p[0] - prev[0][1][0]) / (t - prev[0][0]), (p[1] - prev[0][1][1]) / (t - prev[0][0]))
            sp = math.hypot(*v)
            d = (v[0] / sp, v[1] / sp)
            seg = cv[bisect.bisect_left(ct, t):bisect.bisect_right(ct, t + 1.5)]
            if len(seg) > 5:
                past = max((q[0] - p[0]) * d[0] + (q[1] - p[1]) * d[1] for _, q in seg)
                out.append((round(past, 3), round(t, 2), round(sp, 2)))
            k += len(post)
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
