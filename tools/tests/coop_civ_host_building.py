#!/usr/bin/env python3
"""Civilization: the guest's buildings of the host's civilization show the host's own building designs (BUG-0670).

The class of BUG-0647 (vehicles): the host's civilization names its building designs (city hall, house, factory,
entertainment) by the keys of the host's own editor saves (My Creations, ids sequential per profile); the guest's copy
of that civilization names the same keys, and cCity::SetBuildingModel (0xBDB940) reads that key itself: the guest's
game built the building from the guest's OWN creation of that id (docs/re/civ.md#civ-host-buildings).

Set-up (cold start): the host's My Creations get a base game building model A under a test id X, the guest's My
Creations another base game building model B under the same X. Both games' player civilization name X as their
house design (`coop.civ bdesign`, the state the shared save gives both). The host builds a house in its city (its
game builds A); the guest's game builds its copy (MSG_CIV_BUILD).

PASS: the guest's house of design X is drawn from a model whose record is the host's A (the bem record the guest's
game answers for the building's own model key, building +0x34 +0x90, hashed as A on the host), for the new house
and for the house of the loaded world after the guest joins again.
SPORE_FIX_OFF=civ.host_buildings (or --nofix): must FAIL (the guest shows B, its own creation).

  SPORE_LANE=9 python3 tools/tests/coop_civ_host_building.py [--nofix]
"""
import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import part_substitutes as ps  # noqa: E402

import lane  # noqa: E402
import twoinst  # noqa: E402
from coop_nest_species import fixtures_dir, read_pkg, write_pkg  # noqa: E402
from twoinst import Checks, start  # noqa: E402

X = 0x2903A649  # the test's design id, in both profiles (another creation in each)
BEM, VGROUP = 0x1A99B06B, 0x40636200  # building models
BTYPE = 0x2399BE55
HOUSE = '018EB106'
PLAYER_POL = '800021CA'  # the player civilization of the civ_npc fixture


def two_building_models():
    """two different base game building model records of the lane's Data/Spore_Content.package"""
    path = os.path.join(lane.game_dir(lane.LANE), 'Data', 'Spore_Content.package')
    out = []
    for e in ps.entries(path):
        if e['type'] == BEM and e['group'] == VGROUP:
            d = ps.read(path, e)
            if len(d) > 200 and all(d != o for o in out):
                out.append(d)
            if len(out) == 2:
                return out
    raise AssertionError('fewer than two building models in Spore_Content.package')


def fnv(data):
    h = 0x811C9DC5
    for c in data:
        h = ((h * 0x1000193) & 0xFFFFFFFF) ^ c
    return h


_orig_restore = twoinst.restore_creations
MODELS = []


def restore_with_design(guest=True):
    _orig_restore(guest)
    for d, data in ((lane.HOST_DIR, MODELS[0]), (lane.GUEST_DIR, MODELS[1])):
        p = os.path.join(d, 'EditorSaves.package')
        base = os.path.join(fixtures_dir(), 'creations-host.package' if d == lane.HOST_DIR else 'creations-guest.package')
        recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] != X]
        recs.append((BEM, VGROUP, X, data))
        write_pkg(p, recs)


def buildings(con):
    """[(city, ptr, noun, model instance, own instance)]"""
    out = []
    for ln in con.call('coop.civ bmodels')['out'].splitlines():
        m = re.match(r'\s+bld city=(\d+) (\S+) noun=(\w+) model=(\w+)/\w+/\w+ own=(\w+)', ln)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3), int(m.group(4), 16), int(m.group(5), 16)))
    return out


def model_hash(con, inst):
    m = re.search(r'model \w+ (\w+)', con.call(f'coop.creations modelhash {inst:08X} {BTYPE:08X} {VGROUP:08X}')['out'])
    return m.group(1) if m else '?'


def guest_house(guest):
    """the model hash of the guest's building of design X (its key is X, or ours with X as its own)"""
    for c, p, noun, mk, own in buildings(guest):
        if mk == X or own == X:
            return model_hash(guest, mk), mk
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    MODELS[:] = two_building_models()
    ha, hb = f'{fnv(MODELS[0]):08X}', f'{fnv(MODELS[1]):08X}'
    print(f'  fixture: design {X:08X}: host model {ha}, guest model {hb}', flush=True)
    twoinst.restore_creations = restore_with_design
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix civ.host_buildings off')['out'].strip(), flush=True)
    ck(model_hash(host, X) == ha, f'the host\'s game answers its own model for {X:08X} ({model_hash(host, X)})')
    ck(model_hash(guest, X) == hb, f'the guest\'s game answers its own other model for {X:08X} ({model_hash(guest, X)})')
    for c, who in ((host, 'host'), (guest, 'guest')):
        print(f'  {who}:', c.call(f'coop.civ bdesign {PLAYER_POL} house {X:08X}')['out'].strip(), flush=True)
    city = None
    for ln in host.call('coop.civ')['out'].splitlines():
        m = re.match(r'city (\d+) \S+ pol=\w+ \(player\)', ln)
        if m:
            city = int(m.group(1))
            break
    ck(city is not None, f'the host has a city ({city})')
    print('  host:', host.call(f'coop.civ build {city} house nopay')['out'].strip(), flush=True)
    hh = [b for b in buildings(host) if b[3] == X]
    print(f'  host buildings of {X:08X}: {hh}', flush=True)
    ck(hh and model_hash(host, hh[0][3]) == ha, f'the host\'s game built a house of its design {X:08X}')
    gh, mk = None, None
    t0 = time.time()
    while time.time() - t0 < 40:
        gh, mk = guest_house(guest)
        if gh == ha:
            break
        time.sleep(1)
    print(f'  guest house: model key {mk and f"{mk:08X}"} -> model {gh} (host {ha}, guest own {hb})', flush=True)
    print('  guest:', guest.call('coop.civ bmodels')['out'].strip().splitlines()[-1], '|',
          guest.call('coop.creations designs')['out'].strip(), flush=True)
    ck(gh == ha, f'the guest\'s copy of the host\'s house shows the host\'s design ({gh}, host {ha}, the guest\'s own {hb})')
    # the loaded world: the guest joins again, its game loads the host's world (the house with the design key)
    guest.call('coop.leave')
    time.sleep(3)
    gh_left, _ = guest_house(guest)
    print(f'  guest house after leaving: {gh_left} (its own {hb}: no key of ours left)', flush=True)
    guest.call(lane.JOIN)
    t1 = time.time()
    while time.time() - t1 < 40 and not any(m in guest.call('game.mode')['out'] for m in ('GalaxyGameEntry', 'Load')):
        time.sleep(0.3)
    ck(twoinst.wait_guest_in(host, guest, 180), 'the guest is back in the host\'s world')
    gh2, mk2 = None, None
    t0 = time.time()
    while time.time() - t0 < 40:
        gh2, mk2 = guest_house(guest)
        if gh2 == ha:
            break
        time.sleep(1)
    print(f'  after the rejoin: guest house model key {mk2 and f"{mk2:08X}"} -> {gh2}', flush=True)
    ck(gh2 == ha, f'after a world load the guest\'s house shows the host\'s design ({gh2}, host {ha}, the guest\'s own {hb})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
