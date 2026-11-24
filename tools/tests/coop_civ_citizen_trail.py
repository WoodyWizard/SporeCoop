#!/usr/bin/env python3
"""Civilization: a guest's walking city citizens are where the host's are at the same moment (BUG-1227).

The guest's copies of walking city citizens trailed the host's by 3-6 m and neighbours crossed (civ_citizens' size
check then saw a neighbour's copy at a citizen's place). This test measures what both players see: both games record
the citizens as drawn (`coop.bodyrec start kinds zvnya`: z<host id> the host's citizens and the guest's bound copies, one
clock for both games on one PC, docs/06-testing.md#testing-body-trail) and tools/tests/body_trail.py compares each
copy with ITS host citizen (by identity, not the nearest one) at the same moment, over the samples where the host's
citizen walks. The other mirrored walkers of the same session (NPC proxies n, Civ vehicles v) are measured alike for
comparison.

1. Two games on civ_npc, 25 s to settle (the citizens walk by their own AI on the host).
2. SECS s of rows on both games (y<id>: the guest's walk goals, diagnostics; a<id>: each citizen's current animation).
3. Key frames (kind sameview, for the review by eye): the same camera on both games at a running host citizen.
PASS: the guest's walking copies within LIMIT m of their host citizen in 90 % of the moving samples; in at most CROSSED
of them a copy stands over 2 m off its host citizen with another copy nearer (what civ_citizens' size pairs saw).
SPORE_FIX_OFF=civ.citizen_lead fails both. PASS also: the guest's moving copies play the host's run animation in at least
RUN_SHARE of their moving samples, at most RUN_BELOW_HOST under the host's own share, and standing copies are in the run
at most RUN_STANDING_OVER more often than the host's (BUG-1229: a copy kept the animation it had when it was bound, so it
slid in the standing one or ran in place; SPORE_FIX_OFF=civ.citizen_run fails it). PASS also: the copies of standing host
citizens within STAND_LIMIT m of them in 90 % of the standing samples (BUG-1230: the guest's own behaviour walked a copy
8-12 m off and back; SPORE_FIX_OFF=civ.citizen_hold fails it).

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
import keyviews
import lane
import sameview as sv
from twoinst import Checks, keyframe, start

# BUG-1227: measured with civ.citizen_lead off p90 7.3-7.9 m (lag p50 0.59-0.65 s), with it 1.4-1.7 m (0.04 s); NPC
# proxies p90 0.3-0.5 m, Civ vehicles 0.1 m in the same sessions (labs in the ticket)
LIMIT = 2.5  # m, p90 of a walking citizen's copy from the host's citizen
CROSSED = 0.10  # at most this share of moving samples: the copy over body_trail.TRAIL_M m off and another copy nearer
# BUG-1229: a moving citizen (over RUN_SPEED m/s; they run at 8 m/s) plays the run: on the host in every moving sample; the
# guest's moving copies did in 17-85 % (civ.citizen_run off, labs in the ticket)
RUN_SPEED = 2.0  # m/s
RUN_SHARE = 0.90  # at least this share of the guest's moving copies' samples in the host's run animation
RUN_BELOW_HOST = 0.08  # and at most this much under the host's own share
# BUG-1229: the other half of it: a copy keeps the animation it had when it was bound (labs in the ticket), so one bound in
# the run ran in place while it stood (standing samples in the run: host 5-6 %, guest up to 45 %)
RUN_STANDING_OVER = 0.10  # at most this share of the guest's standing samples in the run over the host's own share
# BUG-1230: while a host citizen stands, the guest's own behaviour walked its copy 8-12 m away and back now and then (the
# mod walked it back only 1.5 s later): standing samples p90 2.3-6.4 m, over 3 m in 7-22 % (6 recordings of fix/w51-trail;
# civ.citizen_hold off 3.41 m); with civ.citizen_hold p90 0.54-0.55 m, max 2.4-2.8 m (labs in the ticket)
STAND_LIMIT = 1.5  # m, p90 of a standing host citizen's copy from it
# BUG-1231: standing host citizens play gestures (neither idle 02481DE5 nor run 02481DB0) in 20-45 % of standing samples;
# with civ.citizen_gesture on, the guest's bound copy plays the same gesture (SPORE_FIX_OFF=civ.citizen_gesture: 0 %)
IDLE_ANIM = '02481DE5'
GESTURE_MATCH = 0.40  # at least this share of standing host gesture samples match on the guest copy


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


def standing(hr, gr):
    """BUG-1230: the distances of the guest's copies from their host citizens (z<id>, by identity, at the guest's distance
    from the centre) over the samples where the host's citizen stands (under body_trail.MOVING m/s)"""
    hb, gb = body_trail._by_key([r for r in hr if r[1][0] == 'z']), body_trail._by_key([r for r in gr if r[1][0] == 'z'])
    out = []
    for key, gv in gb.items():
        hv = hb.get(key)
        if not hv:
            continue
        ht = [x[0] for x in hv]
        for t, gp in gv:
            i = bisect.bisect_left(ht, t)
            h = body_trail.host_at(hv, ht, t)
            if h is None:
                continue
            j0, j1 = max(0, i - 3), min(len(hv) - 1, i + 2)
            dt = hv[j1][0] - hv[j0][0]
            sp = math.dist(hv[j1][1], hv[j0][1]) / dt if dt > 0.02 else 0.0
            rg, rh = body_trail._norm(gp), body_trail._norm(h)
            if sp >= body_trail.MOVING or rg < 1 or rh < 1:
                continue
            out.append(math.dist(gp, tuple(x * rg / rh for x in h)))
    return out


def _anim(p):
    return f'{int(p[1]) << 16 | int(p[0]):08X}'


def anim_report(hr, gr):
    """the run animation (a<id> rows: each citizen's current animation on both games): what the host's moving citizens
    play most; how many of the moving samples play it on each game (BUG-1229: the guest's copies often moved in the
    standing animation); how often the copies of host citizens that run play it too. None without a run"""
    from collections import Counter
    ha, ga = body_trail._by_key([r for r in hr if r[1][0] == 'a']), body_trail._by_key([r for r in gr if r[1][0] == 'a'])
    hz, gz = body_trail._by_key([r for r in hr if r[1][0] == 'z']), body_trail._by_key([r for r in gr if r[1][0] == 'z'])
    hm, gm, both, stand_both = Counter(), Counter(), [], []
    for key, gv in ga.items():
        hv = ha.get(key)
        zk = 'z' + key[1:]
        if not hv or zk not in hz or zk not in gz:
            continue
        ht = [x[0] for x in hv]
        last = -1.0
        for t, gp in gv:
            if t - last < 0.1:
                continue
            last = t
            i = bisect.bisect_right(ht, t) - 1
            if i < 0 or t - ht[i] > 0.25:
                continue
            hs, gs = speed_at(hz[zk], t), speed_at(gz[zk], t)
            if hs is None or gs is None:
                continue
            h_an, g_an = _anim(hv[i][1]), _anim(gp)
            hm[(hs > RUN_SPEED, h_an)] += 1
            gm[(gs > RUN_SPEED, g_an)] += 1
            if hs > RUN_SPEED:
                both.append((h_an, g_an))
            elif gs <= RUN_SPEED:
                stand_both.append((h_an, g_an))
    print(f'  host citizens (moving, animation): {hm.most_common(6)}', flush=True)
    print(f'  guest copies (moving, animation): {gm.most_common(6)}', flush=True)
    run = Counter(a for (m, a), n in hm.items() if m for _ in range(n)).most_common(1)
    if not run:
        return None
    run = run[0][0]

    def share(m, moving=True):
        n = sum(v for (mv, _), v in m.items() if mv == moving)
        return m[(moving, run)] / max(n, 1), n

    walk = [g for h, g in both if h == run]
    pair = sum(1 for g in walk if g == run) / max(len(walk), 1)
    print(f'  the copies of running host citizens (animation {run}) play it too: {pair:.0%} of {len(walk)} samples',
          flush=True)
    gest = [(h, g) for h, g in stand_both if h not in (IDLE_ANIM, run, '00000000')]
    g_match = sum(1 for h, g in gest if g == h) / max(len(gest), 1)
    print(f'  the copies of gesturing standing host citizens match: {g_match:.0%} of {len(gest)} samples', flush=True)
    return run, share(hm), share(gm), share(hm, False), share(gm, False), (g_match, len(gest))


def speed_at(rows, t, win=0.3):
    """a body's speed (m/s) over its rows within win s of t (None without rows there)"""
    i0 = bisect.bisect_left(rows, (t - win,))
    i1 = bisect.bisect_right(rows, (t + win, (1e18,)))
    near = rows[i0:i1]
    if len(near) < 2 or near[-1][0] - near[0][0] < 0.1:
        return None
    return math.dist(near[-1][1], near[0][1]) / (near[-1][0] - near[0][0])


def wobj_citizens(con):
    """this game's citizens: id -> (position, the host id it is bound to)"""
    out = con.call('coop.wobj list citizen 100000 0 0 0')['out']
    return {int(m.group(1)): ([float(m.group(i)) for i in (2, 3, 4)], int(m.group(5)))
            for m in re.finditer(r'id=(\d+) pos=\((\S+) (\S+) (\S+)\) .*?bound_to=(\d+)', out)}


def run_frames(host, guest, n=2):
    """(visual review) the same camera on both games at a host citizen that runs: its copy should run beside it"""
    shot = 0
    for _ in range(12):
        if shot >= n:
            break
        a = wobj_citizens(host)
        time.sleep(0.4)
        b = wobj_citizens(host)
        g = {hid: gid for gid, (_, hid) in wobj_citizens(guest).items() if hid}
        run = [i for i in b if i in a and i in g and math.dist(a[i][0], b[i][0]) > 2.0]
        if not run:
            time.sleep(1)
            continue
        place = b[run[0]][0]
        # BUG-1459: a camera with a clear line of sight to the citizen (one fixed camera stood in a city wall, lab
        # 20261122-194210 run2); the manifest says whether one was found
        cam = {}
        _, _, info = keyviews.clear_view(host, place, 14.0, 5.0, 1.0, terrain=False, cam=cam)
        place = wobj_citizens(host).get(run[0], (place,))[0]  # (it ran on while the view was chosen)
        eye, target = sv.camera_at(place, cam['k'], cam['n'], cam['dist'], cam['height'], 1.0)
        print(f'  run frame at citizen {run[0]}: {info}', flush=True)
        if all('now=1' in (c.call(sv.view_cmd(eye, target, 50)).get('out') or '') for c in (host, guest)):
            time.sleep(0.3)
            keyframe(f'run{shot + 1}', f'the same camera on both games at host citizen {run[0]} that runs (8 m/s): the same '
                     'citizens at the same places, each running (legs apart, the run animation), none sliding while '
                     'it stands', kind='sameview', settle=0, extra=[info])
            shot += 1
        for c in (host, guest):
            c.call('coop.view off')
        time.sleep(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=60)
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(25)
    for c in (host, guest):
        c.call('coop.bodyrec start kinds zvnya')
    time.sleep(a.secs)
    for c in (host, guest):
        c.call('coop.bodyrec stop')
    hr, gr = body_trail.dump(host), body_trail.dump(guest)
    run_frames(host, guest)
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
    st = standing(hr, gr)
    ck(len(st) >= 1000, f'the host\'s citizens stood where the guest has copies ({len(st)} standing samples >= 1000)')
    if st:
        sp90 = body_trail.pct(st, .9)
        ck(sp90 <= STAND_LIMIT, f'a standing citizen\'s copy is within {STAND_LIMIT} m of the host\'s citizen in 90 % of the '
           f'standing samples (p50 {body_trail.pct(st, .5):.2f} p90 {sp90:.2f} m, over 3 m {sum(d > 3 for d in st) / len(st):.1%}, '
           f'max {max(st):.1f} m; else it runs off and back while the host\'s stands, BUG-1230)')
    an = anim_report(hr, gr)
    ck(an is not None and an[2][1] >= 200, f"the guest's copies moved while the host's citizens ran "
       f'({an[2][1] if an else 0} moving samples >= 200)')
    if an:
        run, (h_sh, h_n), (g_sh, g_n), (h_st, h_sn), (g_st, g_sn), (g_match, g_gn) = an
        ck(g_sh >= RUN_SHARE and g_sh >= h_sh - RUN_BELOW_HOST,
           f'a moving copy plays the host\'s run {run}: {g_sh:.0%} of {g_n} moving samples on the guest, host {h_sh:.0%} '
           f'of {h_n} (at least {RUN_SHARE:.0%} and at most {RUN_BELOW_HOST:.0%} under the host; else it slides in its '
           'standing pose, BUG-1229)')
        ck(g_st <= h_st + RUN_STANDING_OVER,
           f'a standing copy is not in the run: {g_st:.0%} of {g_sn} standing samples on the guest, host {h_st:.0%} of '
           f'{h_sn} (at most {RUN_STANDING_OVER:.0%} over the host; else its legs run while it stands, BUG-1229)')
        ck(g_gn >= 40 and g_match >= GESTURE_MATCH,
           f'a standing copy plays the host\'s standing gestures: {g_match:.0%} of {g_gn} gesture samples match '
           f'(at least {GESTURE_MATCH:.0%}, host samples >= 40; else copies stay in plain idle {IDLE_ANIM}, BUG-1231)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
