#!/usr/bin/env python3
"""Tribe: a host member whose velocity says it jumps is shown where it is on the guest (BUG-0759).

campaign_tc2 (lab 20261108-114435, 4 players): the host's chief slept (node 0x04611FF4) with its velocity at 119.7 m/s
(two checks 3 s apart); the guest's mirror stood 11.5 m off at speed 0. The sync-check reported `t: 1 apart (t4 11.5)`.
Not reproduced with this recipe (the jump and the left velocity of that state): a regression test of it (the mirror
follows a jump of the host's member and a velocity no member walks at does not put it ahead), no twin switch.

1. Two games on the tribe fixture. A member of the host's that stands.
2. The host's game puts that member 11.5 m away at once (`coop.tribe put`: the jump) and holds its velocity field at
   120 m/s in that direction for 6 s (`coop.tribe setvel`: the state the game left the chief in).
3. For those 6 s (from 1 s on) the guest's mirror of it stays within 3 m of the host's member (each game's own
   position of it, `coop.tribe`).

No fix switch: the case was not reproduced (BUG-0759 not-reproduced, docs/re/tribe.md#tribe-mirror-jump).

  SPORE_LANE=7 python3 tools/tests/coop_tribe_mirror_jump.py
"""
import math
import re
import time

from twoinst import Checks, start


def rows(con):
    r = {}
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) speed=([\d.]+) host_speed=([\d.]+).*'
                     r'leader=(\d)', line)
        if m:
            r[int(m.group(1))] = dict(owner=int(m.group(2)), pos=tuple(float(m.group(i)) for i in (3, 4, 5)),
                                      speed=float(m.group(6)), hs=float(m.group(7)), leader=m.group(8) == '1')
    return r


def tangent(p, speed):
    """a direction along the ground at p (perpendicular to up), scaled"""
    l = math.sqrt(sum(x * x for x in p)) or 1.0
    n = [x / l for x in p]
    ax = (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)
    t = (n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0])
    tl = math.sqrt(sum(x * x for x in t)) or 1.0
    return tuple(x / tl * speed for x in t)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    mid, t0 = None, time.time()
    while time.time() - t0 < 30 and mid is None:
        h, g = rows(host), rows(guest)
        still = sorted((not v['leader'], i) for i, v in h.items() if v['owner'] == 0 and v['speed'] < 0.1 and i in g and
                       math.dist(v['pos'], g[i]['pos']) < 1.5)
        mid = still[0][1] if still else None
        if mid is None:
            time.sleep(1)
    if not ck(mid is not None, f'a host member that stands, its mirror on the guest at its place ({mid})'):
        return ck.result()
    p_old = rows(host)[mid]['pos']
    # the jump: 11.5 m along the ground at once (`coop.tribe put`, a teleport in the host's game), the velocity left at
    # 120 m/s in that direction
    d = tangent(p_old, 11.5)
    print(f'  member #{mid} at {p_old}; ' + host.call('coop.tribe put %d %.2f %.2f %.2f' % ((mid,) + tuple(
        a + b for a, b in zip(p_old, d))))['out'].strip(), flush=True)
    v = tangent(p_old, 120.0)
    print('  ' + host.call('coop.tribe setvel %d %.2f %.2f %.2f 6' % ((mid,) + v))['out'].strip(), flush=True)
    time.sleep(0.2)
    p0 = rows(host)[mid]['pos']
    t0, worst, moved, hs = time.time(), 0.0, 0.0, 0.0
    while time.time() - t0 < 6:
        time.sleep(0.4)
        h, g = rows(host).get(mid), rows(guest).get(mid)
        if not h or not g:
            continue
        d = math.dist(h['pos'], g['pos'])
        hs = max(hs, g['hs'])
        moved = max(moved, math.dist(h['pos'], p0))
        if time.time() - t0 > 1.0:
            worst = max(worst, d)
        print(f'  {time.time() - t0:4.1f} s: host {tuple(round(x, 1) for x in h["pos"])} speed {h["speed"]:.1f} | '
              f'guest {d:.1f} m off, the state\'s speed {g["hs"]:.1f}', flush=True)
    ck(hs > 100, f'the host sent the member\'s 120 m/s (the guest saw {hs:.0f} m/s)')
    ck(moved < 3, f'the host\'s member itself stayed at its place ({moved:.1f} m)')
    ck(worst < 3, f'the guest\'s mirror stayed within 3 m of the host\'s member ({worst:.1f} m at most)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
