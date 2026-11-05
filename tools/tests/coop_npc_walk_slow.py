#!/usr/bin/env python3
"""Tribe and Creature (--fixture creature, BUG-0590): an animal's proxy keeps up with the host's walking animal on a guest at a low frame rate (BUG-0503).

The monitor's `proxies` rule failed play_tribe / long_tribe (D0's long sessions never passed): a guest's proxy of a
host animal 3-8 m off the host's animal for 2-11 s, "placed on the host's path". coop.npctrack on a loaded lane
showed the guest at 3-5 fps: walk_place closes the error at (speed + 3 m/s) x the frame time, capped at 0.1 s, so
the placed proxy (not only the drawn one) fell 0.2-0.4 m further behind every frame of a 4 m/s walk.

1. Two games on the tribe fixture, both games' time running. The guest runs at 4 fps (`frame.limit 250`).
2. The host's game walks the WALKERS animals nearest its player tribe (`coop.npctrack walk`: SetDesiredSpeed +
   MoveTo, the AI's own way) to a new point 15 m away every 3 s at 4-5 m/s, for SECS s.
3. Checks on the guest's per-frame rows (coop.npctrack, the proxy as the game drew it and the host's interpolated
   state, each at its own distance from the planet's centre, as the monitor compares them):
   * enough frames: at least MIN_ROWS walked rows per walker (a test that saw nothing proves nothing);
   * no walked proxy over 3 m off the host's animal for 2 s (the monitor's rule);
   * the placed position (after the mod's placing) within PLACED_P90 m of the host's state in 90 % of the walked rows.
SPORE_FIX_OFF=npc.proxy_slow_frames must FAIL (the 0.1 s cap).

  SPORE_LANE=4 python3 tools/tests/coop_npc_walk_slow.py [--secs 40] [--fps-ms 250]
"""
import argparse
import math
import random
import re
import time

from coop_npc_walk import family, host_npcs
from coop_sync import wait_running
from twoinst import Checks, start

WALKERS = 4
MIN_ROWS = 40
OFF_M, OFF_S = 3.0, 2.0   # the monitor's proxies rule (npc.cpp monitor_tick)
PLACED_P90 = 1.0          # m


def dump(con):
    rows, i = [], 0
    while True:
        out = [ln.split() for ln in con.call(f'coop.npctrack dump {i} 4000')['out'].splitlines() if ln.strip()]
        rows += out
        i += len(out)
        if len(out) < 4000:
            return rows


def at_radius(p, ref):
    """p taken to ref's distance from the planet's centre (each game its own ground, BUG-0329)"""
    rp, rr = math.sqrt(sum(v * v for v in p)), math.sqrt(sum(v * v for v in ref))
    return tuple(v * rr / rp for v in p) if rp > 1 and rr > 1 else p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=40)
    ap.add_argument('--fps-ms', type=int, default=250)
    ap.add_argument('--fixture', default='tribe')  # creature: the walkers are not the host's family or pack
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    w = wait_running(host, guest, 120)
    ck(w is not None, f'both games\' time runs ({w})')
    m = re.search(r'centre=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.npctrack count')['out'])
    centre = tuple(float(m.group(i)) for i in (1, 2, 3)) if m else (0, 0, 0)
    hn = host_npcs(host)
    if a.fixture == 'creature':
        fam = family(host)
        hn = {k: v for k, v in hn.items() if k not in fam}
    walkers = sorted(hn, key=lambda i: math.dist(hn[i][0], centre))[:WALKERS]
    print(f'  walkers {[(i, round(math.dist(hn[i][0], centre))) for i in walkers]}', flush=True)
    print('  guest: ' + guest.call(f'frame.limit {a.fps_ms}')['out'].strip(), flush=True)
    guest.call('coop.npctrack start 400')
    rng = random.Random(7)
    t0 = time.time()
    while time.time() - t0 < a.secs:
        hn = host_npcs(host)
        for i in walkers:
            if i not in hn:
                continue
            p = hn[i][0]
            ang = rng.uniform(0, 2 * math.pi)
            # a point 15 m away on the tangent plane at p (the planet is a sphere)
            n = [v / math.sqrt(sum(x * x for x in p)) for v in p]
            ax = (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)
            u = (n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0])
            ul = math.sqrt(sum(x * x for x in u))
            u = [x / ul for x in u]
            v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
            to = tuple(p[k] + 15 * (math.cos(ang) * u[k] + math.sin(ang) * v[k]) for k in range(3))
            host.call('coop.npctrack walk %d %.2f %.2f %.2f %.1f' % (i, *to, rng.uniform(4, 5)))
        time.sleep(3)
    guest.call('coop.npctrack stop')
    guest.call('frame.limit 0')
    rows = dump(guest)
    ts = sorted(float(r[0]) for r in rows)
    frames = len(set(ts))
    span = (ts[-1] - ts[0]) if ts else 0
    print(f'  guest rows {len(rows)}, about {frames / max(span, 1):.1f} fps', flush=True)
    for i in walkers:
        mine = sorted((r for r in rows if int(r[1]) == i and r[2] == '3'), key=lambda r: float(r[0]))
        drawn, placed, run_from, worst = [], [], None, 0.0
        for r in mine:
            t = float(r[0])
            d, p, tg = (tuple(map(float, r[k:k + 3])) for k in (5, 8, 11))
            e = math.dist(d, at_radius(tg, d))
            drawn.append(e)
            placed.append(math.dist(p, at_radius(tg, p)))
            if e > OFF_M:
                run_from = t if run_from is None else run_from
                worst = max(worst, t - run_from)
            else:
                run_from = None
        placed.sort()
        p90 = placed[int(len(placed) * 0.9)] if placed else 99
        print(f'  walker {i}: {len(mine)} walked rows, drawn error max {max(drawn or [0]):.2f} m, longest over '
              f'{OFF_M:.0f} m {worst:.1f} s, placed p90 {p90:.2f} m', flush=True)
        ck(len(mine) >= MIN_ROWS, f'walker {i}: the guest walked its proxy ({len(mine)} rows)')
        ck(worst < OFF_S, f'walker {i}: never {OFF_M:.0f} m off the host\'s animal for {OFF_S:.0f} s ({worst:.1f} s)')
        ck(p90 <= PLACED_P90, f'walker {i}: placed on the host\'s path (p90 {p90:.2f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
