#!/usr/bin/env python3
"""Civilization: a guest's walking city citizens are where the host's are at the same moment (BUG-1227).

The guest's copies of walking city citizens trailed the host's by 3-6 m and neighbours crossed (civ_citizens' size
check then saw a neighbour's copy at a citizen's place). This test measures what both players see: both games record
the citizens as drawn (`coop.bodyrec start kinds zvny`: z<host id> the host's citizens and the guest's bound copies, one
clock for both games on one PC, docs/06-testing.md#testing-body-trail) and tools/tests/body_trail.py compares each
copy with ITS host citizen (by identity, not the nearest one) at the same moment, over the samples where the host's
citizen walks. The other mirrored walkers of the same session (NPC proxies n, Civ vehicles v) are measured alike for
comparison.

1. Two games on civ_npc, 25 s to settle (the citizens walk by their own AI on the host).
2. SECS s of rows on both games (y<id>: the guest's walk goals, diagnostics).
3. Every GAIT_EVERY s both games' `coop.wobj list citizen` (each copy's animation next to its host citizen's).
PASS: the guest's walking copies within LIMIT m of their host citizen in 90 % of the moving samples; in at most CROSSED
of them a copy stands over 2 m off its host citizen with another copy nearer (what civ_citizens' size pairs saw); the
copies of host citizens in their walk animation play it too (the glide does not stop the walk). SPORE_FIX_OFF=
civ.citizen_lead fails the first two.

  SPORE_LANE=4 python3 tools/tests/coop_civ_citizen_trail.py [--secs 60]
"""
import argparse
import bisect
import json
import math
import os
import re
import time

import body_trail
import lane
from twoinst import Checks, start

# BUG-1227: measured with civ.citizen_lead off p90 7.3-7.9 m (lag p50 0.59-0.65 s), with it 1.4-1.7 m (0.04 s); NPC
# proxies p90 0.3-0.5 m, Civ vehicles 0.1 m in the same sessions (labs in the ticket)
LIMIT = 2.5  # m, p90 of a walking citizen's copy from the host's citizen
CROSSED = 0.10  # at most this share of moving samples: the copy over body_trail.TRAIL_M m off and another copy nearer
WALK_ANIM = '02481DB0'  # a citizen's walk / run animation (what most walking citizens play on both games)
WALK_OK = 0.80  # at least this share of the copies of host citizens playing it play it too (the glide keeps the walk)


def crossings(hr, gr, own_min=0.0):
    """over every guest frame and walking host citizen z<id> with a copy: (samples, samples where another copy stands
    nearer the host's citizen than its own copy, worst examples)"""
    hb, gb = body_trail._by_key([r for r in hr if r[1][0] == 'z']), {}
    frames = {}
    for t, k, p in gr:
        if k[0] == 'z':
            frames.setdefault(t, {})[k] = p
    n = bad = 0
    worst = []
    hts = {k: [x[0] for x in v] for k, v in hb.items()}
    for t, copies in frames.items():
        for k, gp in copies.items():
            hv = hb.get(k)
            if not hv:
                continue
            h = body_trail.host_at(hv, hts[k], t)
            if h is None:
                continue
            i = bisect.bisect_left(hts[k], t)
            j0, j1 = max(0, i - 3), min(len(hv) - 1, i + 2)
            dt = hv[j1][0] - hv[j0][0]
            sp = math.dist(hv[j1][1], hv[j0][1]) / dt if dt > 0.02 else 0.0
            if sp < body_trail.MOVING or sp > 60:
                continue
            own = math.dist(gp, h)
            other = min(((math.dist(q, h), kk) for kk, q in copies.items() if kk != k), default=(1e9, ''))
            n += 1
            if other[0] < own and own > own_min:
                bad += 1
                worst.append((own, k, other[1], other[0], t))
    return n, bad, sorted(worst, reverse=True)[:4]


GAIT_EVERY = 3.0
ROW = re.compile(r'id=(\d+) .*?bound_to=(\d+) .*?gait=(-?\d+) anim=([0-9A-F]+)')


def gait_pairs(host, guest):
    """the walk gait (game::gait_state, 0: none) and animation of each host citizen and the guest's copy of it"""
    def rows(con):
        out = con.call('coop.wobj list citizen 100000 0 0 0')['out']
        return {int(m.group(1)): (int(m.group(2)), int(m.group(3)), m.group(4)) for m in ROW.finditer(out)}
    h, g = rows(host), rows(guest)
    return [(h[hid][1], gg, h[hid][2], an) for _, (hid, gg, an) in g.items() if hid and hid in h]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=60)
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(25)
    for c in (host, guest):
        c.call('coop.bodyrec start kinds zvny')
    gaits = []  # (host gait, guest copy's gait) of every bound pair, every GAIT_EVERY s
    t_end = time.time() + a.secs
    while time.time() < t_end:
        time.sleep(min(GAIT_EVERY, max(0.0, t_end - time.time())))
        if time.time() < t_end:
            gaits += gait_pairs(host, guest)
    for c in (host, guest):
        c.call('coop.bodyrec stop')
    hr, gr = body_trail.dump(host), body_trail.dump(guest)
    path = os.path.join(lane.out_dir('track'), f'civ_citizen_trail_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'host': hr, 'guest': gr}, f)
    print(f'  rows: host {len(hr)}, guest {len(gr)}: {path}', flush=True)
    t_first = min((r[0] for r in hr), default=0)
    res = body_trail.analyse(hr, gr, kinds='znv')
    for line in body_trail.summary(res, t_first):
        print('  ' + line.replace('z:', 'city citizens:', 1), flush=True)
    # (diagnostic) the guest's walk goals y<id> from the host's citizen at the same moment
    goals = body_trail.analyse(hr, [(t, 'z' + k[1:], p) for t, k, p in gr if k[0] == 'y'], kinds='z').get('z')
    if goals and goals['n']:
        print(f'  walk goals from the host\'s citizen: p50 {body_trail.pct(goals["dist"], .5):.2f} '
              f'p90 {body_trail.pct(goals["dist"], .9):.2f} m', flush=True)
    print('  ' + ' '.join(ln.strip() for ln in guest.call('coop.wobj')['out'].splitlines() if ln.startswith(('kind citizen', 'citizens '))), flush=True)
    if gaits:
        from collections import Counter
        print(f'  gaits (host, guest copy): {Counter((a, b) for a, b, _, _ in gaits).most_common(8)}', flush=True)
        print(f'  anims (host, guest copy): {Counter((a, b) for _, _, a, b in gaits).most_common(8)}', flush=True)
    r = res.get('z')
    ck(r is not None and r['n'] >= 300, f'the host\'s citizens walked where the guest has copies ({r["n"] if r else 0} moving samples >= 300)')
    if r and r['n']:
        p50, p90 = body_trail.pct(r['dist'], .5), body_trail.pct(r['dist'], .9)
        ck(p90 <= LIMIT, f'a walking citizen\'s copy is within {LIMIT} m of the host\'s citizen in 90 % of the moving samples '
           f'(p50 {p50:.2f} p90 {p90:.2f} m, lag p50 {body_trail.pct(r["lag"], .5):.2f} s)')
    n, bad, worst = crossings(hr, gr, body_trail.TRAIL_M)
    ck(n and bad / n <= CROSSED, f'a walking citizen\'s copy stands over {body_trail.TRAIL_M:g} m off with another copy nearer in '
       f'{bad / max(n, 1):.1%} of {n} moving samples (at most {CROSSED:.0%}: a neighbour\'s copy at its place)'
       + ('; e.g. ' + ', '.join(f'{k} own {d:.1f} m, {o} {od:.1f} m at {t - t_first:.1f} s' for d, k, o, od, t in worst[:3])
          if worst else ''))
    walk = [g for h, _, ha, g in gaits if ha == WALK_ANIM]
    share = sum(1 for g in walk if g == WALK_ANIM) / max(len(walk), 1)
    ck(len(walk) >= 20 and share >= WALK_OK, f'the copies of host citizens in their walk animation play it too: {share:.0%} of '
       f'{len(walk)} (at least {WALK_OK:.0%}, >= 20 samples)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
