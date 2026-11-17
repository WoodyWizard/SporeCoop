#!/usr/bin/env python3
"""Civilization: a wild creature's copy on the guest stays on the guest's ground after the host's creature jumped onto
lower ground (BUG-1216, kind B).

In the 4-player campaign's Civ segment (lab 20261116-085558) a guest drew two walking animals 8.2 m over its ground
(`agl=8.23 want_agl=0.02 grounded=0 tp=1`): the proxy was driven at its own distance from the planet's centre
(own_height, BUG-0329); one snap to where the host's creature was now (a jump of its target) onto ground 8 m lower
kept that distance, and the Civ game does not ground a creature placed in the air (Tribe's does).

1. Two games on civ_npc (a herd of 7 and three epics), both cameras on the herd.
2. A place along the ground 25-90 m from the herd whose terrain lies 3-9.5 m lower than the herd's (the host's
   `coop.abilities ground`), where the herd would drop to after the jump.
3. The host's game sets its herd there, onto its terrain (`coop.civ creatureshift ... ground`: a jump of the
   creatures, as a herd taken back into the pool and put out again, or a flight).
4. For 8 s from 3 s after the jump: the host's creatures of that herd and the guest's copies, their heights over
   each game's own terrain (`coop.npc list` agl, where the game draws them).
   * precondition: the host's creatures stand on the host's ground (median agl under 0.5 m), else INCONCLUSIVE;
   * every copy on the guest within 1 m of its ground in every sample (BUG-1216: 3-9.5 m up before);
   * every copy within 6 m of its host creature along the ground (they are where the host has them).
SPORE_FIX_OFF=npc.civ_ground: the copies hang over the guest's ground by the drop: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_civ_proxy_ground.py
"""
import math
import re
import statistics
import time

from coop_civ_wild_sync import wild
from play_civ import dist, frame_city
from twoinst import Checks, keyframe, start

DROP_MIN, DROP_MAX = 3.0, 9.5  # m: the off side keeps the proxy's height only within 10 m (kOwnHeightMax)
FLOAT_MAX = 1.0                # m over the guest's ground
NEAR_MAX = 6.0                 # m along the ground


def ground(con, p):
    m = re.search(r'ground ([-\d.]+)', con.call('coop.abilities ground %.2f %.2f %.2f' % tuple(p))['out'])
    return float(m.group(1)) if m else 0.0


def host_rows(con):
    """{id: (pos, agl)} of the host's NPCs (coop.npc list)"""
    out = {}
    for ln in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) \S+ species=\S+ .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).* agl=([-\d.]+)', ln)
        if m:
            out[int(m.group(1))] = ((float(m.group(2)), float(m.group(3)), float(m.group(4))), float(m.group(5)))
    return out


def guest_rows(con):
    """{id: (drawn pos, agl)} of the guest's spawned proxies (coop.npc list)"""
    out = {}
    for ln in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) species=\S+ spawned .*? at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) agl=([-\d.]+)', ln)
        if m:
            out[int(m.group(1))] = ((float(m.group(2)), float(m.group(3)), float(m.group(4))), float(m.group(5)))
    return out


def along_ground(a, b):
    """the distance of two points at the radius of a (their heights over the centre left out)"""
    ra, rb = math.hypot(*a), math.hypot(*b)
    return dist(a, tuple(v * ra / rb for v in b))


def tangent(c):
    """creatureshift's direction along the ground at c (civ_debug.cpp)"""
    ln = math.hypot(*c)
    up = tuple(v / ln for v in c)
    t = (up[1], -up[0], 0.0) if abs(up[2]) <= 0.9 else (0.0, up[2], -up[1])
    tl = math.hypot(*t)
    return tuple(v / tl for v in t)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(5)
    herd = [x[0] for x in wild(host) if x[1]]
    if not ck(len(herd) >= 3, f'wild creatures in the host\'s game ({len(herd)})'):
        return ck.result()
    c0 = max(herd, key=lambda p: sum(1 for q in herd if dist(p, q) < 30))
    frame_city({'host': host, 'guest': guest}, c0, answer=False)
    time.sleep(3)
    # 2. lower ground along creatureshift's direction
    g0, t, r0 = ground(host, c0), tangent(c0), math.hypot(*c0)
    best = None
    for d in [s * k for k in range(25, 91, 5) for s in (1, -1)]:
        q = tuple(c0[i] + t[i] * d for i in range(3))
        drop = g0 - ground(host, q)
        if DROP_MIN <= drop <= DROP_MAX and (best is None or drop > best[1]):
            best = (d, drop, q)
    if best is None:
        return ck.inconclusive(f'no ground {DROP_MIN}-{DROP_MAX} m lower within 25-90 m of the herd at '
                               f'{tuple(round(v) for v in c0)}')
    d, drop, q = best
    print(f'  herd at {tuple(round(v) for v in c0)} (ground {g0:.2f}, radius {r0:.2f}); {d} m aside the ground lies '
          f'{drop:.2f} m lower', flush=True)
    before = host_rows(host)
    ids = sorted(i for i, (p, _) in before.items() if dist(p, c0) < 30)
    gb = guest_rows(guest)
    print(f'  the herd: host ids {ids}; guest copies before: '
          + ' '.join(f'#{i} agl={gb[i][1]:.2f}' for i in ids if i in gb), flush=True)
    if not ck(ids and all(i in gb for i in ids), f'the guest has copies of the herd ({len(ids)} creatures)'):
        return ck.result()
    # 3. the host's herd jumps onto the lower ground
    x, y, z = c0
    print('  host:', host.call(f'coop.civ creatureshift {x:.1f} {y:.1f} {z:.1f} 30 {d} ground')['out'].strip(), flush=True)
    frame_city({'host': host, 'guest': guest}, q, answer=False)
    time.sleep(3)
    # 4. 8 s of samples
    hagl, worst_up, worst_off, rows = [], {}, {}, []
    t0 = time.time()
    while time.time() - t0 < 8:
        h, g = host_rows(host), guest_rows(guest)
        for i in ids:
            if i in h and i in g:
                hagl.append(h[i][1])
                worst_up[i] = max(worst_up.get(i, -99.0), g[i][1])
                worst_off[i] = max(worst_off.get(i, 0.0), along_ground(h[i][0], g[i][0]))
        rows.append(' '.join(f'#{i} {h[i][1]:.2f}/{g[i][1]:.2f}' for i in ids if i in h and i in g))
        time.sleep(1)
    for r in rows:
        print(f'    agl host/guest: {r}', flush=True)
    keyframe('herd', 'the herd on the lower ground: on the ground on both screens', players={'host': host, 'guest': guest})
    print('  guest', guest.call('coop.npc status')['out'].strip().splitlines()[1][-120:], flush=True)
    if not hagl:
        return ck.inconclusive('no sample with the herd on both games')
    hmed = statistics.median(hagl)
    if hmed > 0.5:
        return ck.inconclusive(f'the host\'s creatures are not on the host\'s ground (median agl {hmed:.2f} m)')
    up = max(worst_up.values())
    ck(up <= FLOAT_MAX, f'every copy on the guest\'s ground after the jump (highest {up:.2f} m over it, at most '
       f'{FLOAT_MAX}; the drop was {drop:.2f} m): ' + ' '.join(f'#{i} {v:.2f}' for i, v in sorted(worst_up.items())))
    off = max(worst_off.values())
    ck(off <= NEAR_MAX, f'every copy where the host has its creature along the ground (farthest {off:.1f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
