#!/usr/bin/env python3
"""Civilization: where each game puts a planet's city citizens (BUG-0526: level of detail or desync?).

The detector had a city's citizens in one game only (`guest_only:citizen` with 15 on the guest and none on the host
at a city, `host_only:citizen`). The question: does each game put its citizens at the cities near its own camera
(a pool, as the Space stage's colony citizens: docs/re/space.md#space-planet-life), or does a city lose or keep its
citizens in one game?

1. Two games on civ_npc, both cameras at the capital: citizens per city and in all in each game.
2. The guest's camera pans to the NPC city farthest from the capital (W/A/S/D as a player); the host's stays.
3. 10 s later the same counts; then the guest's camera back to the capital, 10 s, the same.
PASS (level of detail): in each game the total stays within 10 % while the guest's camera is away; at the far
city the guest's game has citizens and the host's none; back at the capital both games have them there again.
(A report test: it decides how the detector treats the rows.)

  SPORE_LANE=6 python3 tools/tests/coop_civ_citizen_lod.py
"""
import math
import random
import re
import time

from play_civ import Player, civ_state
from twoinst import Checks, start

CITIZEN = '18EB4B7'


def citizens(con):
    return [tuple(float(x) for x in m.groups()) for m in
            re.finditer(r'^obj \S+ (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)', con.call(f'coop.civ nouns {CITIZEN}')['out'], re.M)]


def per_city(cits, cities, r=90):
    out = {}
    for c, d in cities.items():
        out[c] = sum(1 for p in cits if math.dist(p, d['pos']) < r)
    return out


def counts(host, guest, cities, what):
    h, g = citizens(host), citizens(guest)
    hp, gp = per_city(h, cities), per_city(g, cities)
    print(f'  {what}: host {len(h)} in all {hp}; guest {len(g)} in all {gp}', flush=True)
    return len(h), len(g), hp, gp


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    cities, _, _ = civ_state(host)
    home = next(c for c, d in cities.items() if d['player'])
    npc = [c for c, d in cities.items() if not d['player']]
    far = max(npc, key=lambda c: math.dist(cities[c]['pos'], cities[home]['pos']))
    print(f'  capital {home}, far city {far} ({math.dist(cities[far]["pos"], cities[home]["pos"]):.0f} m)', flush=True)
    gp = Player('guest', guest, 1, random.Random(1), [])
    gp.look_at(cities[home]['pos'])
    time.sleep(8)
    h0, g0, hp0, gp0 = counts(host, guest, cities, 'both at the capital')
    seen = gp.look_at(cities[far]['pos'])
    print(f'  the guest\'s camera at city {far}: {seen}', flush=True)
    want = max(1, gp0[home] // 2)  # most of the guest's city citizens (BUG-0553: 1 of 19 when the mod put them back)
    # the game moves its pool to the city near the camera within 10 to 30 s (once 0 after 10 s on a loaded machine)
    for _ in range(7):
        time.sleep(10 if _ == 0 else 3)
        h1, g1, hp1, gp1 = counts(host, guest, cities, f'the guest\'s camera at city {far}')
        if gp1[far] >= want:
            break
    gp.look_at(cities[home]['pos'])
    time.sleep(10)
    h2, g2, hp2, gp2 = counts(host, guest, cities, 'the guest\'s camera back at the capital')
    ck(seen is not None, f'the guest\'s camera reached city {far}')
    ck(abs(g1 - g0) <= max(2, 0.1 * g0) and abs(h1 - h0) <= max(2, 0.1 * h0),
       f'each game keeps its number of citizens (host {h0} -> {h1}, guest {g0} -> {g1}): a pool')
    ck(gp1[far] >= want and hp1[far] == 0,
       f'at city {far} the guest\'s game has its citizens ({gp1[far]}, at least {want}), the host\'s none ({hp1[far]})')
    ck(gp2[home] > 0 and hp2[home] > 0, f'back at the capital both games have citizens there ({hp2[home]} / {gp2[home]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
