#!/usr/bin/env python3
"""Civilization: the ground round every city is the same on the host and the guest (probe for the BUG-0332 class).

BUG-0332 (Tribe, fix/w45-village-ground, docs/re/tribe.md#tribe-terrain-stamps): terrain stamps applied live
(Terrain ApplyStamp 0xFA6C10) render another ground than the same stamp list after a load. Civ cities apply
stamps live too (cCityData / cCity). This measures whether the two games' grounds differ round the cities:

1. Two games on civ_npc (the cities of the save: loaded on both).
2. The ground (`coop.ground`, cPlanetModel::GetHeightAt) on a grid round every city (up to 80 m), both games.
3. The host founds a new NPC city (SpawnNextCity, broadcast: the guest founds it live too) and captures an NPC
   city (cCity::Capture); 30 s later the grid again, round every city (the new one too).
PASS: at every point the two grounds differ by at most 0.3 m.

  SPORE_LANE=4 python3 tools/tests/coop_civ_city_ground.py [--no-change]
"""
import argparse
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start

LIMIT = 0.3


def ground(con, p):
    m = re.search(r'ground ([-\d.]+)', con.call(f'coop.ground {p[0]:.2f} {p[1]:.2f} {p[2]:.2f}')['out'])
    return float(m.group(1)) if m else None


def cap_points(c, cap_m=80.0, step_m=10.0):
    r = math.dist(c, (0, 0, 0))
    up = tuple(x / r for x in c)
    a = (0.0, 0.0, 1.0) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
    e1 = (a[1] * up[2] - a[2] * up[1], a[2] * up[0] - a[0] * up[2], a[0] * up[1] - a[1] * up[0])
    l = math.dist(e1, (0, 0, 0))
    e1 = tuple(x / l for x in e1)
    e2 = (up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0])
    pts = [c]
    d = step_m
    while d <= cap_m:
        n = max(6, int(2 * math.pi * d / step_m))
        t = d / r
        for k in range(n):
            ph = 2 * math.pi * k / n
            q = tuple(math.cos(t) * up[i] + math.sin(t) * (math.cos(ph) * e1[i] + math.sin(ph) * e2[i]) for i in range(3))
            pts.append(tuple(x * r for x in q))
        d += step_m
    return pts


def survey(host, guest, label):
    hc = civ_state(host)[0]
    worst, bad, total = 0.0, [], 0
    for ci, c in sorted(hc.items()):
        cb = []
        for p in cap_points(c['pos']):
            h, g = ground(host, p), ground(guest, p)
            if h is None or g is None:
                continue
            total += 1
            if abs(h - g) > LIMIT:
                cb.append((p, h, g))
        w = max((abs(h - g) for _, h, g in cb), default=0.0)
        worst = max(worst, w)
        print(f'  {label}: city {ci} pol={c["pol"]}{" (player)" if c["player"] else ""}: {len(cb)} points differ, '
              f'largest {w:.2f} m' + (f' e.g. ({cb[0][0][0]:.0f} {cb[0][0][1]:.0f} {cb[0][0][2]:.0f}) host {cb[0][1]:.2f} '
                                      f'guest {cb[0][2]:.2f}' if cb else ''), flush=True)
        bad += cb
    print(f'  {label}: {total} points, {len(bad)} differ by more than {LIMIT} m, the largest {worst:.2f} m', flush=True)
    return bad, worst, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--no-change', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    bad0, w0, n0 = survey(host, guest, 'loaded')
    ck(n0 > 0, f'ground measured round the cities ({n0} points)')
    ck(not bad0, f'loaded cities: the same ground on both games ({len(bad0)} points off, largest {w0:.2f} m)')
    if not a.no_change:
        m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
        if m:
            print('  host SpawnNextCity:', host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0')['out'].strip(), flush=True)
        hc = civ_state(host)[0]
        npc = sorted(i for i, c in hc.items() if not c['player'])
        if npc:
            print('  host:', host.call(f'coop.civ capture {npc[0]}')['out'].strip(), flush=True)
        time.sleep(30)
        bad1, w1, n1 = survey(host, guest, 'after a founding and a capture')
        ck(not bad1, f'after a live founding and a capture: the same ground ({len(bad1)} points off, largest {w1:.2f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
