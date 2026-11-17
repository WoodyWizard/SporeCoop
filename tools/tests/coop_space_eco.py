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
   cargo drop calls: PlanetEco_AddPlant 0xC81C50 / PlanetEco_AddAnimal 0xC82590; an animal also gets the drop's own
   creature at that spot, PlanetEco_DropAnimal 0xAD0450; the cargo drop itself is not driven: the planet's story scene
   covers the palette).
3. The host's record of the planet (by PlanetID, the host away), then the host lands on it.
PASS: the guest's planet is stabilized (level 1); the host's record has the guest's plant and animal species in the
same order and the game's own stabilized level 1 while away and after landing, and the host's planet shows creatures
of each of the guest's animal species; the terrain cell at each spot where the guest planted holds that plant in the
host's game too (BUG-1224: the flora is drawn per cell, so the same trees stand there; key frame `*_spot`, the same
camera over the spot). SPORE_FIX_OFF=space.eco_sync must FAIL (the host keeps 2 plants, level 0);
SPORE_FIX_OFF=space.eco_spots must FAIL the cell check (the species reach the record, the trees stand elsewhere) and,
where the mod makes the creature, the size check (an epic of the species).
--live: the host lands on the planet first, so the guest's species reach a game that is on it (the game's placement
functions and a herd there, not the record alone); the same checks on the planet.

  SPORE_LANE=6 python3 tools/tests/coop_space_eco.py [--live]
"""
import sys
import re
import time

import keyviews
import lane
from play_space import Player
from twoinst import Checks, keyframe, start

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


def cell(con, pos):
    """BUG-1224: the plant species of the terrain grid cell of a point on the planet this game is on (one per size class,
    the game's flora there is drawn from them): (cell index, [keys])"""
    out = sp(con, 'eco cell', *(f'{v:.1f}' for v in pos))
    m = re.match(r'cell (\d+) n=\S+ (.*?) rest', out)
    if not m:
        return None, []
    return int(m.group(1)), [k for k, n in re.findall(r'p\d=([0-9A-F:]{26})/(-?\d+)', m.group(2)) if k != '00000000:00000000:00000000']


def ground_point(con, pos):
    m = re.search(r'ground ([\d.]+) point ([\d.]+)', con.call('coop.ground %.1f %.1f %.1f' % tuple(pos))['out'])
    if not m or float(m.group(2)) < 1:
        return tuple(pos)
    f = float(m.group(1)) / float(m.group(2))
    return tuple(v * f for v in pos)


def spot_frame(host, guest, spot, tag):
    """BUG-1224: both cameras at the same place over the spot where the guest planted (70 m away, 45 m up: the trees of
    the cell); the same trees must stand there in both games"""
    from coop_space_map import no_movie
    try:
        for con in (host, guest):
            no_movie(con, 20)
        keyviews.look_both((host, guest), spot, dist=70, height=45, look_up=3, settle=1.0)
        keyframe(tag, 'space, both on the planet the guest filled, the same camera 70 m from and 45 m above the spot where '
                      'the guest planted: the same trees (the flora of that terrain cell) in both games',
                 extra=[f'spot {tuple(round(v, 1) for v in spot)}'], settle=0)
    finally:
        keyviews.view_off(host, guest)


def eco_frame(host, guest, animals, tag):
    """a key frame of the planet's new life on both games (BUG-1220): both cameras at the same place, 30 m from and 18 m
    above the host's nearest creature of a species the guest placed (the host's herd of it). A herd's place is each
    game's own: the planet entry makes the herds of the planet's species at spots of its own choosing on every landing
    (docs/re/space.md#space-eco-spots), so the guest's creature (the seam's drop creature) is elsewhere."""
    from coop_space_map import no_movie
    import sameview as sv
    inst = {k.split(':')[0].upper() for k in animals}
    seen = {}
    try:
        for role, con in (('host', host), ('guest', guest)):
            no_movie(con, 20)  # the planet's story caption ("Something is showing up on the ship's radar")
            rows = sv.parse_census(con.call('coop.census creatures').get('out') or '')
            r = keyviews.nearest_row(con, lambda r: r['kind'] == 'c' and r['alive'] and r['key'] in inst)
            seen[role] = (r and (r['key'], r['pos']), f'{len(rows)} creature rows')
        r = keyviews.nearest_row(host, lambda r: r['kind'] == 'c' and r['alive'] and r['key'] in inst)  # (where it is now)
        if r:
            keyviews.look_both((host, guest), r['pos'], dist=30, height=18, look_up=3, settle=0.5)
        print(f'  key frame cameras at the placed species: {seen}', flush=True)
        keyframe(tag, 'space, both on the planet the guest filled, the same camera 30 m from and 18 m above the host\'s creature of a '
                      'species the guest placed (its herd there); each game places a herd where its planet entry chose, '
                      'so the guest\'s own creature of it may be elsewhere', extra=[f'{k}: {v}' for k, v in seen.items()], settle=0)
    finally:
        keyviews.view_off(host, guest)


def creature_hp(con, keys):
    """{species instance: [hp of each living creature of it]} in this game (the game's own creature list)"""
    out = {}
    for m in re.finditer(r'key=([0-9A-Fa-f]{8}) alive=1 hp=([\d.]+)', con.call('coop.census creatures').get('out') or ''):
        if m.group(1).upper() in keys:
            out.setdefault(m.group(1).upper(), []).append(float(m.group(2)))
    return out


def check_normal(ck, host, animals):
    """BUG-1224: the host's creatures of the guest's species are animals of their own size: BUG-1148's herd spawn
    (PlanetEco_SpawnHerd, the planet's epic spawn) made one epic-size creature of each (hp 3500, scale ~15)"""
    keys = {k.split(':')[0].upper() for k in animals}
    hp = creature_hp(host, keys)
    big = {k: max(v) for k, v in hp.items() if max(v) >= 1000}
    print(f'  host creatures of the placed species (hp): { {k: sorted(v)[-3:] for k, v in hp.items()} }', flush=True)
    ck(hp and not big, f'the host\'s creatures of the guest\'s animal species are of their own size, no epic ({big})')


def check_spots(ck, host, guest, plant_spots):
    """BUG-1224: the host's terrain cell at each spot where the guest planted holds that plant species, as the guest's
    does (the game draws a cell's flora from its species: the same trees at the same place)"""
    rows, ok = [], bool(plant_spots)
    deadline = time.time() + 15
    while True:
        rows, ok = [], bool(plant_spots)
        for k, p in plant_spots:
            hc, hk = cell(host, p)
            gc, gk = cell(guest, p)
            rows.append(f'{k[:8]} cell {hc}/{gc} host {[x[:8] for x in hk]} guest {[x[:8] for x in gk]}')
            ok = ok and k in gk and k in hk and hc == gc
        if ok or time.time() > deadline:
            break
        time.sleep(2)
    print('  spots: ' + ' | '.join(rows), flush=True)
    ck(ok, f'the plants the guest placed are in the same terrain cell in the host\'s game (where it planted them): {rows}')


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
    spots = []  # (what, key, the point the guest placed it at)
    for what, i in picks:
        out = sp(guest, 'eco place', what, i, SRC).strip()
        print('  guest:', out, flush=True)
        m = re.search(r'placed \w+ (\S+) at (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)', out)
        if m:
            spots.append((what, m.group(1), tuple(float(m.group(k)) for k in (2, 3, 4))))
        time.sleep(1.5)
    plant_spots = [(k, p) for w, k, p in spots if w == 'plant']
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
        check_spots(ck, host, guest, plant_spots)
        check_normal(ck, host, ga)
        eco_frame(host, guest, ga, 'eco_live')
        if plant_spots:
            spot_frame(host, guest, ground_point(guest, plant_spots[0][1]), 'eco_live_spot')
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
    check_spots(ck, host, guest, plant_spots)
    check_normal(ck, host, ga)
    eco_frame(host, guest, ga, 'eco_landed')
    if plant_spots:
        spot_frame(host, guest, ground_point(guest, plant_spots[0][1]), 'eco_landed_spot')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
