#!/usr/bin/env python3
"""BUG-0787: a guest's copy of a host vehicle that is more than twice as fast as the copy's own top speed follows it
smoothly (before: the copy stood and jumped).

The guest takes a step between two snapshots faster than `2 x top + 10` as a jump of the host's game (BUG-0186) and
sets the copy's velocity to 0. `top` was the copy's own +0x218, which the host's alarm boost (GitHub #199, stacked by
BUG-0771) or a faster host design does not change.

1. Two games on civ_npc; the host's player vehicle (the free one of the capital) gets the top speed 55 m/s
   (+0x218 written with mem.write; its copy on the guest keeps its own ~19 m/s: limit 48 m/s).
2. The host orders it back and forth between two points 300 m apart for 20 s (`coop.civ move`).
3. Both games record the vehicle as drawn (`coop.bodyrec`, one clock for both). At every guest frame where the host's
   vehicle drives (> 30 m/s), the host's vehicle at the same moment is interpolated: the guest's copy must be within
   2.5 m of it at the median and 4.5 m at p90. Measured (lab 20261112-003654 / -003659): without the fix every
   snapshot step is a jump, the velocity is 0 and the copy is only glided half way per frame: it trails 4.8 m (p50),
   6.8 m (p90) at 55 m/s (it does not stand still, as the ticket guessed); with the fix 0.7 / 1.8 m.
Then +0x218 is put back.

  SPORE_FIX_OFF=civ.vel_cap_host coop_civ_fast_mirror.py   (must FAIL)
"""
import bisect
import math
import re
import struct
import time

import body_trail
from play_civ import civ_state
from twoinst import Checks, start

FAST = 55.0
ADDR = re.compile(r'^\s+veh #(\d+) (?:0x)?([0-9A-Fa-f]+) ', re.M)


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * (len(xs) - 1)))] if xs else 0.0


def track(rows, key):
    r = sorted((t, p) for t, k, p in rows if k == key)
    return [t for t, _ in r], [p for _, p in r]


def at(ts, ps, t):
    i = bisect.bisect_left(ts, t)
    if i <= 0 or i >= len(ts) or ts[i] - ts[i - 1] > 0.25:
        return None
    f = (t - ts[i - 1]) / (ts[i] - ts[i - 1]) if ts[i] > ts[i - 1] else 0.0
    return tuple(ps[i - 1][j] + (ps[i][j] - ps[i - 1][j]) * f for j in range(3))


def speed_at(ts, ps, t, w=0.1):
    a, b = at(ts, ps, t - w / 2), at(ts, ps, t + w / 2)
    return math.dist(a, b) / w if a and b else None


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    cities, hv, _ = civ_state(host)
    mine = [i for i, v in hv.items() if 0 <= v['owner'] < 4]
    ck(mine, f'the host has a player vehicle: {mine}')
    if not mine:
        return ck.result()
    vid = mine[0]
    addr = int({int(m.group(1)): m.group(2) for m in ADDR.finditer(host.call('coop.civ')['out'])}[vid], 16)
    rd = host.call(f'mem.read 0x{addr + 0x218:08X} 4')['out']
    own = struct.unpack('<f', bytes(int(x, 16) for x in rd.split()[1:5]))[0]
    host.call(f'mem.write 0x{addr + 0x218:08X} {struct.pack("<f", FAST).hex()}')
    gline = next((ln for ln in guest.call('coop.civ')['out'].splitlines() if re.match(rf'\s+veh #{vid} ', ln)), '')
    m = re.search(r' top=([\d.]+)', gline)
    gtop = float(m.group(1)) if m else None
    print(f'  vehicle #{vid}: own top speed {own:.2f}, now {FAST} on the host; the guest copy\'s own {gtop}', flush=True)
    ck(gtop is not None and 2 * gtop + 10 < FAST * 0.95,
       f'the copy\'s own jump limit (2 x {gtop} + 10) is under the host vehicle\'s speed (precondition)')
    p0 = hv[vid]['pos']
    c = cities[min(cities)]['pos'] if cities else p0
    dx, dz = p0[0] - c[0], p0[2] - c[2]
    n = math.hypot(dx, dz) or 1.0
    pts = [(p0[0] + 150 * dx / n, p0[1], p0[2] + 150 * dz / n), (p0[0] - 150 * dx / n, p0[1], p0[2] - 150 * dz / n)]
    for con in (host, guest):
        body_trail.record(con, True)
    t0, k = time.time(), 0
    while time.time() - t0 < 20:
        tgt = pts[k % 2]
        host.call(f'coop.civ move {vid} {tgt[0]:.1f} {tgt[1]:.1f} {tgt[2]:.1f}')
        k += 1
        t1 = time.time()
        while time.time() - t1 < 8 and time.time() - t0 < 20:
            v = civ_state(host)[1].get(vid)
            if v and math.dist(v['pos'], tgt) < 25:
                break
            time.sleep(0.2)
    for con in (host, guest):
        body_trail.record(con, False)
    host.call(f'mem.write 0x{addr + 0x218:08X} {struct.pack("<f", own).hex()}')
    hrows, grows = body_trail.dump(host), body_trail.dump(guest)
    keys = sorted({kk for _, kk, _ in hrows})
    key = next((kk for kk in keys if kk.startswith('v') and re.sub(r'\D', '', kk) == str(vid)), None)
    print(f'  rows host {len(hrows)} guest {len(grows)}; vehicle keys {[kk for kk in keys if kk.startswith("v")]} -> {key}')
    ck(key, 'the vehicle is in both games\' records (precondition)')
    if not key:
        return ck.result()
    hts, hps = track(hrows, key)
    gts, gps = track(grows, key)
    dists, slow, n_fast, top = [], 0, 0, 0.0
    for t, gp in zip(gts, gps):
        hp = at(hts, hps, t)
        hs = speed_at(hts, hps, t)
        if hp is None or hs is None:
            continue
        top = max(top, hs)
        if hs < 30 or hs > 120:
            continue
        n_fast += 1
        dists.append(math.dist(gp, hp))
        gs = speed_at(gts, gps, t)
        slow += gs is not None and gs < hs / 3
    p90 = pct(dists, 0.9)
    frac = slow / n_fast if n_fast else 1.0
    st = civ_state(guest)[2]
    print(f'  {n_fast} guest frames with the host vehicle over 30 m/s (its top {top:.1f} m/s): distance p50 '
          f'{pct(dists, 0.5):.1f} p90 {p90:.1f} max {max(dists or [0]):.1f} m; copy standing in {slow} ({frac:.0%})',
          flush=True)
    print('  guest:', ' '.join(ln.strip() for ln in guest.call('coop.civ')['out'].splitlines()
                              if re.match(rf'\s+veh #{vid} ', ln))[-160:], flush=True)
    ck(n_fast >= 60, f'the host vehicle drove over 30 m/s in {n_fast} guest frames (precondition)')
    p50 = pct(dists, 0.5)
    ck(p50 < 2.5 and p90 < 4.5, f'the guest\'s copy is with the host\'s vehicle (distance p50 {p50:.1f} < 2.5 m, '
                                f'p90 {p90:.1f} < 4.5 m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
