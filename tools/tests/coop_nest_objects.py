#!/usr/bin/env python3
"""The objects at a nest of another species on the host and on the guest (player report 2026-11-01: "in
enemy nests eggs are desynced and bugged (flying mid air)").

1. Two games on the creature fixture. The host picks the nearest nest of another species with members
   (`coop.npc archless all`), both avatars go next to it (`coop.npc gopos`).
2. Both list every game object within RADIUS m of their avatar (`coop.npctrack objects`: noun, position,
   height over the ground) and the objects are matched by noun and position.
3. Reported: objects of a noun only one game has, objects whose height over the ground differs by more
   than LIMIT m, objects more than LIMIT m over the ground on the guest (floating). PASS when no object of
   the nest floats on the guest while the host has it on the ground.

  coop_nest_objects.py [--nests N] [--radius R]"""
import argparse
import math
import re
import time

from twoinst import Checks, shot, start

LIMIT = 0.75
SKIP_NOUNS = set()


def objects(con, r):
    out = []
    for m in re.finditer(r'obj (\S+) noun=(\S+) pos=\((\S+) (\S+) (\S+)\) over_ground=(\S+) alive=(\d) dist=(\S+)',
                         con.call(f'coop.npctrack objects {r}')['out']):
        out.append({'p': m.group(1), 'noun': m.group(2), 'pos': tuple(map(float, m.group(3, 4, 5))),
                    'og': float(m.group(6)), 'alive': int(m.group(7)), 'd': float(m.group(8))})
    return out


def nests(con):
    out = []
    for m in re.finditer(r'herd (\S+) arch=(\S+) species=\S+/(\S+) nest=(\S+) dist=(\d+) en=(\d) members=(\d+) in_world=(\d+) '
                         r'near_home=(\d+) home=\((\S+) (\S+) (\S+)\)', con.call('coop.npc archless all')['out']):
        if m.group(4) not in ('00000000', '(nil)'):
            out.append((int(m.group(5)), m.group(1), m.group(3), tuple(map(float, m.group(10, 11, 12)))))
    return sorted(out)


def beside(p, d):
    r = math.sqrt(sum(x * x for x in p))
    up = [x / r for x in p]
    a = [0.0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0.0, up[0]]
    al = math.sqrt(sum(x * x for x in a))
    return [p[i] + a[i] / al * d + up[i] * 2 for i in range(3)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nests', type=int, default=3)
    ap.add_argument('--radius', type=float, default=20)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    ns = nests(host)[:a.nests]
    print('  nests of other species (dist, herd, species, home):', ns, flush=True)
    ck(ns, 'a nest of another species on the host')
    floating_total = 0
    for k, (dist, herd, sp, home) in enumerate(ns):
        q = beside(home, 6)
        for c, side in ((host, 0), (guest, 1)):
            qq = beside(q, 2.5 * side)
            print(f'  {"host" if c is host else "guest"}:', c.call('coop.npc gopos %.1f %.1f %.1f' % tuple(qq))['out'].strip(), flush=True)
        time.sleep(12)  # a nest far from the start gets its members and eggs when a player is near
        shot(guest, f'nest{k}_guest')
        shot(host, f'nest{k}_host')
        ho, go = objects(host, a.radius), objects(guest, a.radius)
        hn, gn = {}, {}
        for o in ho:
            hn.setdefault(o['noun'], []).append(o)
        for o in go:
            gn.setdefault(o['noun'], []).append(o)
        print(f'  nest {k} ({sp}, {dist} m from the start): host {len(ho)} objects, guest {len(go)}', flush=True)
        for noun in sorted(set(hn) | set(gn)):
            h, g = hn.get(noun, []), gn.get(noun, [])
            hog = sorted(round(o['og'], 2) for o in h)
            gog = sorted(round(o['og'], 2) for o in g)
            # match each guest object to the nearest host object of the noun
            pairs = []
            for o in g:
                best = min(h, key=lambda x: math.dist(x['pos'], o['pos'])) if h else None
                if best:
                    pairs.append((round(math.dist(best['pos'], o['pos']), 2), round(o['og'] - best['og'], 2)))
            print(f'    noun {noun}: host {len(h)} over ground {hog[:8]}, guest {len(g)} over ground {gog[:8]}; '
                  f'guest->nearest host (m apart, height diff) {pairs[:8]}', flush=True)
            fl = [o for o in g if o['og'] > LIMIT and not any(math.dist(x['pos'], o['pos']) < LIMIT for x in h)]
            floating_total += len(fl)
            if fl:
                print(f'      floating on the guest only: {[(o["p"], round(o["og"], 2)) for o in fl]}', flush=True)
    ck(floating_total == 0, f'nothing floats on the guest where the host has it on the ground ({floating_total})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
