#!/usr/bin/env python3
"""Civilization: the guest's mirror of a host vehicle shows the host's own vehicle design (BUG-0647).

Player report 2026-11-06 (0.7.8): the guest saw "nightmarish monsters" where the host had its ground
vehicles. The host's civilization names its vehicle designs by the keys of the host's own editor saves
(My Creations, ids sequential per profile); the guest's copy of that civilization names the same keys,
and the guest's game resolves them to the guest's OWN creation of that id.

Set-up (cold start): the host's My Creations get a base game vehicle model A under a test id X, the
guest's My Creations another base game vehicle model B under the same X (two profiles that both made a
creation with that id). Both games' player civilization name X as their military land design
(`coop.civ setdesign`, the state the shared save gives both). The host spawns a military land vehicle
(its game builds A).

PASS: the guest's mirror of that vehicle is built from a model whose record is the host's A (the bem
record the guest's game answers for the mirror's own model key, vehicle +0xC4, hashed the same as A on
the host), for the new vehicle and for a vehicle of the loaded world the guest binds.
--nofix (`coop.fix creations.host_designs off`): must FAIL (the mirror shows B, the guest's own creation).

  SPORE_LANE=4 python3 tools/tests/coop_civ_host_design.py [--nofix]
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

X = 0x2903A647  # the test's design id, in both profiles (another creation in each)
BEM, VGROUP = 0x1A99B06B, 0x40646200
PLAYER_POL = '800021CA'  # the player civilization of the civ_npc fixture


def two_vehicle_models():
    """two different base game vehicle model records of the lane's Data/Spore_Content.package"""
    path = os.path.join(lane.game_dir(lane.LANE), 'Data', 'Spore_Content.package')
    out = []
    for e in ps.entries(path):
        if e['type'] == BEM and e['group'] == VGROUP:
            d = ps.read(path, e)
            if len(d) > 200 and all(d != o for o in out):
                out.append(d)
            if len(out) == 2:
                return out
    raise AssertionError('fewer than two vehicle models in Spore_Content.package')


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


def vehicles(con):
    out = {}
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) \S+ pol=(\w+) .*purpose=(\d+) loco=(\d+).* model=(\w+)', ln)
        if m:
            out[int(m.group(1))] = (m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5), 16))
    return out


def model_hash(con, inst):
    m = re.search(r'model \w+ (\w+)', con.call(f'coop.creations modelhash {inst:08X}')['out'])
    return m.group(1) if m else '?'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    MODELS[:] = two_vehicle_models()
    ha, hb = f'{fnv(MODELS[0]):08X}', f'{fnv(MODELS[1]):08X}'
    print(f'  fixture: design {X:08X}: host model {ha}, guest model {hb}', flush=True)
    twoinst.restore_creations = restore_with_design
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix creations.host_designs off')['out'].strip(), flush=True)
    ck(model_hash(host, X) == ha, f'the host\'s game answers its own model for {X:08X} ({model_hash(host, X)})')
    ck(model_hash(guest, X) == hb, f'the guest\'s game answers its own other model for {X:08X} ({model_hash(guest, X)})')
    for c, who in ((host, 'host'), (guest, 'guest')):
        print(f'  {who}:', c.call(f'coop.civ setdesign {PLAYER_POL} 0 0 {X:08X}')['out'].strip(),
              c.call(f'coop.civ vdesign {PLAYER_POL} 0 0')['out'].strip(), flush=True)
    before = set(vehicles(host))
    city = None
    for ln in host.call('coop.civ')['out'].splitlines():
        m = re.match(r'city (\d+) \S+ pol=\w+ \(player\)', ln)
        if m:
            city = int(m.group(1))
            break
    ck(city is not None, f'the host has a city ({city})')
    print('  host:', host.call(f'coop.civ spawnveh {city} 0 0')['out'].strip(), flush=True)
    new, hv, gv = None, {}, {}
    t0 = time.time()
    while time.time() - t0 < 40:
        hv, gv = vehicles(host), vehicles(guest)
        fresh = [i for i in hv if i not in before and hv[i][3] == X]
        if fresh and fresh[0] in gv and gv[fresh[0]][3]:
            new = fresh[0]
            time.sleep(2)
            gv = vehicles(guest)
            break
        time.sleep(1)
    print(f'  host vehicles {hv}\n  guest vehicles {gv}', flush=True)
    print('  guest:', guest.call('coop.creations designs')['out'].strip(), flush=True)
    ck(new is not None, f'the host\'s new vehicle of design {X:08X} is mirrored on the guest ({new})')
    if new is not None:
        mk = gv[new][3]
        gh = model_hash(guest, mk)
        print(f'  guest mirror #{new}: model key {mk:08X} -> model {gh} (host {ha}, guest own {hb})', flush=True)
        ck(gh == ha, f'the guest\'s mirror of the host\'s vehicle shows the host\'s design ({gh}, host {ha}, '
                     f'the guest\'s own creation {hb})')
        twoinst.keyframe('mirror', 'the host\'s new vehicle and the guest\'s mirror of it look alike where they are on '
                                   'screen (the host\'s design, not the guest\'s creation of that id), the Civ HUD in place')
    if new is not None:
        # a vehicle of the loaded world: the guest joins again, its game loads the host's world (with that
        # vehicle and the design key in the save) and binds the loaded vehicle as the mirror
        guest.call('coop.leave')
        time.sleep(3)
        guest.call(lane.JOIN)
        t1 = time.time()
        while time.time() - t1 < 40 and not any(m in guest.call('game.mode')['out'] for m in ('GalaxyGameEntry', 'Load')):
            time.sleep(0.3)
        ck(twoinst.wait_guest_in(host, guest, 180), 'the guest is back in the host\'s world')
        hv2, gv2, gh2 = {}, {}, '?'
        t0 = time.time()
        while time.time() - t0 < 40:
            hv2, gv2 = vehicles(host), vehicles(guest)
            mine = [i for i in hv2 if hv2[i][0] == PLAYER_POL and hv2[i][3] == X]
            if mine and mine[0] in gv2:
                gh2 = model_hash(guest, gv2[mine[0]][3])
                if gh2 == ha:
                    break
            time.sleep(1)
        print(f'  after the rejoin: host {hv2}\n  guest {gv2}', flush=True)
        print('  guest:', guest.call('coop.civ tables')['out'].strip()[-60:], '|',
              guest.call('coop.creations designs')['out'].strip(), flush=True)
        ck(gh2 == ha, f'after a world load the guest\'s mirror of that vehicle shows the host\'s design ({gh2}, host {ha}, '
                      f'the guest\'s own {hb})')
        twoinst.keyframe('after_rejoin', 'after the guest\'s rejoin: both in Civ play (no loading screen), the vehicles '
                                         'with the host\'s designs')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
