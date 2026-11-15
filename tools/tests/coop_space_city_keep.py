#!/usr/bin/env python3
"""Space: a colony building the mod builds (build_in_city) is still in the city after the ship leaves the planet and
lands again (BUG-0901).

Every colony building the mod makes in Space (the host's copy of a guest's planner building, BUG-0751; a kept one,
BUG-0766; a guest's copy of the host's, BUG-0884) goes through civ.cpp build_in_city. The game makes a planet's cities
again from what it keeps for the planet when the planet becomes active (docs/re/space.md#space-colony-build-hold):
a house destroyed at run time came back.

1. Two games on space_empire, both land on the home planet 00460035 (9 full cities).
2. In each game one city: a house destroyed (`coop.civ unbuild`, the game's cCity::DestroyBuilding: a free plot),
   then a factory built there by the mod's build_in_city (`coop.civ sbuild`). A factory, so that the house the game
   brings back in that plot cannot pass for the mod's building.
3. Both ships take off (the system view) and land on the planet again.
PASS: each city has the factory after the landing (factories +1 against the start) in both games.
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, shot, start

HOME = '00460035'
HOUSE, FACTORY = 'noun=018EB106', 'noun=018EA2CC'


def kinds(con, ci):
    bl = con.call(f'coop.civ buildings s{ci}')['out'].splitlines()
    rows = [ln for ln in bl if ln.startswith('building ')]
    return len(rows), sum(HOUSE in ln for ln in rows), sum(FACTORY in ln for ln in rows), rows


def city_positions(con):
    return {int(m.group(1)): tuple(float(m.group(i)) for i in (2, 3, 4))
            for m in re.finditer(r's(\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.civ scities')['out'])}


def find_city(con, pos):
    return next((k for k, p in city_positions(con).items() if sum((x - y) ** 2 for x, y in zip(p, pos)) < 100), None)


def main():
    host, guest = start('space_empire')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    for p in (ph, pg):
        p.land_retry(HOME)
    ck(ph.ctx() == 'planet' and ph.place() == pg.place(), f'both on the home planet ({ph.place()}, {pg.place()})')
    time.sleep(6)
    pos = {}
    start_counts = {}
    for con, who, ci in ((host, 'host', 0), (guest, 'guest', 1)):
        cp = city_positions(con).get(ci)
        pos[who] = cp
        n, h, f, rows = kinds(con, ci)
        start_counts[who] = (n, h, f)
        k = next((i for i, ln in enumerate(rows) if HOUSE in ln), None)
        print(f'  {who} s{ci} at {cp}: {n} buildings, {h} houses, {f} factories; '
              f'{con.call(f"coop.civ unbuild s{ci} {k}")["out"]}', flush=True)
        time.sleep(1)
        out = con.call(f'coop.civ sbuild s{ci} factory')['out']
        n1, h1, f1, _ = kinds(con, ci)
        print(f'  {who}: {out}: {n1} buildings, {h1} houses, {f1} factories', flush=True)
        ck(out.startswith('built') and f1 == f + 1 and h1 == h - 1,
           f'the {who}\'s city has the mod\'s factory in the freed plot ({h} -> {h1} houses, {f} -> {f1} factories)')
    for p in (ph, pg):
        ck(p.take_off(), f'the {p.name}\'s ship leaves the planet ({p.ctx()}, at {p.place()})')
    time.sleep(8)
    for p in (ph, pg):
        p.land_retry(HOME)
    ck(ph.ctx() == 'planet' and ph.place() == pg.place(),
       f'both landed on the home planet again ({ph.place()}, {pg.place()})')
    time.sleep(8)
    for con, who in ((host, 'host'), (guest, 'guest')):
        ci = find_city(con, pos[who]) if pos[who] else None
        n0, h0, f0 = start_counts[who]
        if ci is None:
            ck(False, f'the {who}\'s city is there again')
            continue
        n2, h2, f2, rows = kinds(con, ci)
        shot(con, f'space_city_keep_{who}.png')
        print(f'  {who} s{ci} after the landing: {n2} buildings, {h2} houses, {f2} factories', flush=True)
        ck(f2 == f0 + 1, f'the {who}\'s city still has the mod\'s factory after leaving and landing '
           f'({f0} -> {f2} factories, {h0} -> {h2} houses)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
