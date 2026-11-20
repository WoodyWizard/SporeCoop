#!/usr/bin/env python3
"""Civilization: the host's shot observer and shooters / targets that are no listed object (BUG-1322).

The 0.7.15 hotfix line's copy of rel/0.7.16's `coop_civ_influence.py --case shotobjs` (the other cases of that file test
0.7.16 features; their helpers new_vehicle / put_near are copied as they are there). Two games on civ_npc.

The host's shot observer (squadron's FireTool hook -> civ::fire_observed) took the object at a target spatial - 0x34
and called its vtable when its first dword lay in the game's image; a preacher's target spatial is +0x100 of
Simulator/cCulturalTarget, so target - 0x34 is a field of it, and lab 20261119-114030 (preachers at a far city) faulted
in the observer (docs/re/civ.md#civ-shot-observer-targets). Religious vehicles preach at the nearest NPC city (on its
ground, the players' way: the attack order) for 45 s: the shooters and targets of their shots that are no listed object
are printed (evidence, not a check). Then `coop.civ shotprobe`: one observed shot of a listed host vehicle at a target
that is no object, the memory before it starting with an address in the image. Checked: the observer takes it as a
target the guests cannot find (kind 2) and no game faults.

  SPORE_FIX_OFF=civ.shot_target_listed: the observer calls into the probe's fake vtable and the host faults (FAIL).
"""
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start


def new_vehicle(host, home, purpose):
    # `force`: the civ_npc fixture's civilization (one military city) has no religious or economic vehicle allowance
    # (VehicleSlotsLeft 0 even with all its vehicles removed: the allowance 0xBF2110 counts cities of that purpose;
    # lab runs 20261119-104136, 20261119-114425), so these are spawned past it
    _, before, _ = civ_state(host)
    print('  host spawnveh:', host.call(f'coop.civ spawnveh {home} {purpose} 0 force')['out'].strip(), flush=True)
    time.sleep(2)
    _, after, _ = civ_state(host)
    new = [i for i, v in after.items() if i not in before and v['purpose'] == purpose and v['owner'] >= 0]
    return new[0] if new else None


def put_near(host, vid, pos, d):
    # d m along x from the city, then back onto the sphere of the city's distance from the centre: a point d m off along x
    # alone is up in the air (lab run 20261119-114030: 90 m off, 34 m over the ground)
    p = (pos[0] + d, pos[1], pos[2])
    k = math.hypot(*pos) / math.hypot(*p)
    return host.call(f'coop.civ vput {vid} {p[0] * k:.1f} {p[1] * k:.1f} {p[2] * k:.1f}')['out'].strip()


def case_shot_objects(host, guest, ck, cities, home, near):
    vids = [new_vehicle(host, home, 1) for _ in range(2)]
    vids = [v for v in vids if v is not None]
    ck(vids, f'the host has religious vehicles ({vids})')
    for k, v in enumerate(vids):
        print(f'  #{v}:', put_near(host, v, cities[near]['pos'], -40 - 10 * k), '|',
              host.call(f'coop.civ attack {v} {near} 3')['out'].strip(), flush=True)
    time.sleep(45)
    print('  host', host.call('coop.civ shotobjs')['out'].strip(), flush=True)
    out = host.call('coop.civ shotprobe')['out'].strip()
    print('  host', out, flush=True)
    time.sleep(2)
    m = re.search(r'target kind (-?\d+)', out)
    ck(m and m.group(1) == '2', 'the shot observer takes a target that is no object as one the guests cannot find, calling '
       f'nothing on it ({m and m.group(1)})')
    ck('total faults: 0' in host.call('exc')['out'].splitlines()[0], "no fault in the host's game from the shot observer")


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    cities, _, _ = civ_state(host)
    mine = [c for c, d in cities.items() if d['player']]
    npc = [c for c, d in cities.items() if not d['player'] and d['owner'] < 0]
    if not ck(mine and npc, f'the host has a city ({mine}) and there are NPC cities ({npc})'):
        return ck.result()
    home = mine[0]
    near = min(npc, key=lambda c: math.dist(cities[c]['pos'], cities[home]['pos']))
    print(f'  home {home}, nearest NPC city {near}', flush=True)
    case_shot_objects(host, guest, ck, cities, home, near)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
