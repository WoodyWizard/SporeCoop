#!/usr/bin/env python3
"""Space: the plants and animals a guest places on a planet are on the host's planet too, which is stabilized
there as well (BUG-1148, docs/re/space.md#space-ecosystem).

A planet keeps its species in its record (cPlanetRecord +0xBC plants, +0xD0 animals); the food web and the planet's
stabilized level (SporeCoop::PlanetRecord_EcoLevel 0xBBCFB0: the highest T level up to the T score with 3 animal
species per level) are read from them. Each game kept its own lists, so a guest's populated planet was empty and not
stabilized on the host. The mod now sends the lists after the player's own placement (space_eco.cpp K_ECO); a game
off that planet sets its record's lists, a game on it adds the species through the game's own functions.

1. Two games on space_free; the guest lands on 02460035 (T1: two plants, no animal, stabilized level 0), the host
   stays in the system view (another place).
2. The guest places the missing plant size class and two herbivores and a carnivore of its home planet's species,
   each through the game's own placement function with the player flag (`coop.space eco place`, the function a landed
   cargo drop calls: PlanetEco_AddPlant 0xC81C50 / PlanetEco_AddAnimal 0xC82590; the cargo drop itself is not driven:
   the planet's story scene covers the palette).
3. The host's record of the planet (by PlanetID, the host away), then the host lands on it.
PASS: the guest's planet is stabilized (level 1); the host's record has the guest's plant and animal species in the
same order and the game's own stabilized level 1 while away and after landing, and the host's planet shows creatures
of each of the guest's animal species. SPORE_FIX_OFF=space.eco_sync must FAIL (the host keeps 2 plants, level 0).
--live: the host lands on the planet first, so the guest's species reach a game that is on it (the game's placement
functions and a herd there, not the record alone); the same checks on the planet.

  SPORE_LANE=6 python3 tools/tests/coop_space_eco.py [--live]
"""
import sys
import re
import time

import lane
from play_space import Player
from twoinst import Checks, start

PLANET = '02460035'
SRC = '00460035'  # the home planet: 9 plants, 9 animals (6 herbivores, 3 carnivores)


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out']


def eco(con, planet=''):
    """(level, plants [(key, class)], animals [(key, diet, creatures or None)], text) of the planet's record"""
    out = sp(con, 'eco', planet)
    m = re.search(r'tscore=(-?\d+) eco=(-?\d+)', out)
    plants = re.findall(r'([0-9A-F]{8}:[0-9A-F]{8}:[0-9A-F]{8})/([0-9A-F]{8})', out.split('plants', 1)[-1].split('\n', 1)[0])
    al = out.split('animals', 1)[-1].split('\n', 1)[0] if 'animals' in out else ''
    animals = [(k, d, int(n) if n else None)
               for k, d, n in re.findall(r'([0-9A-F]{8}:[0-9A-F]{8}:[0-9A-F]{8})/([hc?])(?:#(\d+))?', al)]
    return (int(m.group(2)) if m else -1), plants, animals, out.strip()


def main():
    host, guest = start('space_free')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    want = 0x40000000 | int(PLANET, 16)
    live = '--live' in sys.argv
    for _ in range(3):
        pg.land_retry(PLANET)
        if pg.place()[0] == want:
            break
    if live:
        for _ in range(3):
            ph.land_retry(PLANET)
            if ph.place()[0] == want:
                break
        ck(pg.place()[0] == want and ph.place()[0] == want, f'both are on the planet {PLANET} ({pg.place()}, {ph.place()})')
    else:
        ck(pg.place()[0] == want and ph.place() != pg.place(),
           f'the guest is on the planet {PLANET}, the host elsewhere ({pg.place()}, {ph.place()})')
    time.sleep(3)
    lv0, p0, a0, t0 = eco(guest)
    _, hp0, ha0, _ = eco(host, PLANET)
    print(f'  guest before: {t0}', flush=True)
    ck(lv0 == 0 and len(p0) < 3 and not a0, f'the planet is not stabilized before (level {lv0}, {len(p0)} plants)')
    _, sp_, sa, ts = eco(guest, SRC)
    have = {c for _, c in p0}
    picks = []
    for i, (k, c) in enumerate(sp_):
        if c not in have and c != '00000000':
            picks.append(('plant', i))
            have.add(c)
    herb = [i for i, (k, d, _) in enumerate(sa) if d == 'h'][:2]
    carn = [i for i, (k, d, _) in enumerate(sa) if d == 'c'][:1]
    picks += [('animal', i) for i in herb + carn]
    ck(len(herb) == 2 and len(carn) == 1, f'the home planet has two herbivores and a carnivore to place ({ts})')
    for what, i in picks:
        print('  guest:', sp(guest, 'eco place', what, i, SRC).strip(), flush=True)
        time.sleep(1.5)
    time.sleep(3)
    lv1, p1, a1, t1 = eco(guest)
    print(f'  guest after: {t1}', flush=True)
    ck(lv1 == 1 and len(a1) == 3, f"the guest's planet is stabilized at T1 (level {lv1}, {len(a1)} animal species)")
    gp = [k for k, _ in p1]
    ga = [k for k, _, _ in a1]
    hl = None
    if live:
        t_end = time.time() + 30
        while time.time() < t_end:
            time.sleep(3)
            hl, hp, ha, ht = eco(host)
            if hl == 1 and ha and all(n for _, _, n in ha):
                break
        print(f'  host on the planet: {ht}', flush=True)
        print('  ' + sp(host, 'eco').splitlines()[0], flush=True)
        ck(hl == 1 and sorted(k for k, _ in hp) == sorted(gp) and sorted(k for k, _, _ in ha) == sorted(ga),
           f"the host's planet has the guest's species and is stabilized (level {hl})")
        ck(ha and all(n for _, _, n in ha),
           f"the host sees creatures of every animal species the guest placed ({[(k[:8], n) for k, _, n in ha]})")
        ck.faults(host=host, guest=guest)
        return ck.result()
    t_end = time.time() + 20
    while time.time() < t_end:
        hl, hp, ha, ht = eco(host, PLANET)
        if [k for k, _ in hp] == gp and [k for k, _, _ in ha] == ga and hl == 1:
            break
        time.sleep(2)
    print(f'  host away: {ht}', flush=True)
    ck([k for k, _ in hp] == gp and [k for k, _, _ in ha] == ga,
       f"the host's record has the guest's species while away ({len(hp)}/{len(gp)} plants, {len(ha)}/{len(ga)} animals)")
    ck(hl == 1, f"the host's game counts the planet stabilized while away (level {hl})")
    for _ in range(3):
        ph.land_retry(PLANET)
        if ph.place()[0] == want:
            break
    ck(ph.place()[0] == want, f'the host landed on {PLANET} ({ph.place()})')
    t_end = time.time() + 30
    while time.time() < t_end:
        time.sleep(3)
        hl, hp, ha, ht = eco(host)
        if hl == 1 and ha and all(n for _, _, n in ha):
            break
    print(f'  host on the planet: {ht}', flush=True)
    print('  ' + sp(host, 'eco').splitlines()[0], flush=True)
    ck(hl == 1 and [k for k, _ in hp] == gp and [k for k, _, _ in ha] == ga,
       f"on the planet the host's record and stabilized level are the guest's (level {hl})")
    ck(ha and all(n for _, _, n in ha),
       f"the host sees creatures of every animal species the guest placed ({[(k[:8], n) for k, _, n in ha]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
