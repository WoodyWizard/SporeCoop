#!/usr/bin/env python3
"""Civilization: the guest orders its vehicles to attack a wild creature; the host's vehicles carry it out (BUG-0970).

rel/0.7.13 integration (out/lab/20261112-155526, play_civ): the host rejected three orders of the guest at once,
"target kind 3 id 0 noun 018EB45E at (-391 320 -68) not in the host's game": the guest's bot right-clicked a
destination for its three vehicles and a wild creature stood under the cursor, so the guest's game ordered an
attack of that creature (SetOrder type 1). Wild creatures are not synced in Civ: each game runs its own, so the
host found no creature of that noun within 10 m of the guest's position and dropped the orders. The player saw
the vehicles ignore the click.

1. Two games on civ; the host spawns two military land vehicles and gives them to the guest (`coop.give`).
2. The host's game sets its wild creatures around the guest's chosen one 80 m aside (`coop.civ creatureshift`):
   the herds of the two games no longer match, as after a chase, a flight or a death on one game.
3. The guest's game orders each vehicle to attack the creature nearest to it (`coop.civ attack <id> c`: the
   game's SetOrder of the guest, as a right-click on the creature).
4. For up to 40 s, on the host: the vehicles' order (the game's order list) and position; then the guest's
   vehicles (mirrors of the host's) on the guest's screen.
PASS: the host's vehicles took an order (an attack of a creature near there, or a move there) and drive to where
the guest's creature is, and the host rejected no command of the guest.
FAIL switch: `coop.fix civ.creature_target off` (the order is rejected unless the host has a creature within 10 m).

  SPORE_LANE=4 python3 tools/tests/coop_civ_guest_creature.py [--off]
"""
import re
import sys
import time

import worlddiff
from play_civ import civ_state, dist, stat
from twoinst import Checks, start

CREATURE = 0x018EB45E


def creatures(con):
    """the wild creatures of this game (census creature rows, noun 0x18EB45E; the citizens have their own noun)"""
    w = worlddiff.World(con.call('coop.census creatures')['out'])
    return [r['pos'] for r in w.rows if r['noun'] == CREATURE and r['alive']]


def main():
    off = '--off' in sys.argv
    host, guest = start('civ')
    ck = Checks()
    if off:  # (also SPORE_FIX_OFF=civ.creature_target)
        print('  host:', host.call('coop.fix civ.creature_target off')['out'].strip(), flush=True)
    # the herd (wild creatures are the same on both games after the load, within ~3 m: lab 2026-11-12) and the
    # player's city nearest to it: the vehicles are bought there
    gc0 = creatures(guest)
    cities, before, _ = civ_state(host)
    mine = [i for i, c in cities.items() if c['player']]
    ck(gc0 and mine, f'wild creatures in the guest\'s game ({len(gc0)}) and a city of the player ({mine})')
    if not gc0 or not mine:
        return ck.result()
    herd = gc0[0]
    ci = min(mine, key=lambda i: dist(cities[i]['pos'], herd))
    for _ in range(2):
        print(f'  host: city {ci}:', host.call(f'coop.civ spawnveh {ci} 0 0')['out'].strip(), flush=True)
    time.sleep(2)
    _, hv, _ = civ_state(host)
    ids = sorted(set(hv) - set(before))
    for i in ids:
        host.call(f'coop.civ select {i} 1')
    print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
    time.sleep(4)
    _, gv, _ = civ_state(guest)
    ck(len(ids) == 2 and all(gv.get(i, {}).get('owner') == 1 for i in ids), f'the guest owns the two vehicles {ids}')
    if len(ids) != 2 or any(i not in gv for i in ids):
        return ck.result()
    at = gv[ids[0]]['pos']
    gc = creatures(guest)
    near = min(gc, key=lambda p: dist(p, at)) if gc else None
    ck(near is not None, 'a wild creature near the guest\'s vehicles in the guest\'s game')
    if near is None:
        return ck.result()
    # the host's herd is elsewhere now (in play the herds of the two games drift apart, flee, or die on one game:
    # wild creatures are not synced in Civ): the host's game sets its creatures around that place 80 m aside
    x, y, z = near
    print('  host:', host.call(f'coop.civ creatureshift {x:.1f} {y:.1f} {z:.1f} 40 80')['out'].strip(), flush=True)
    hc = creatures(host)
    off_host = min((dist(near, p) for p in hc), default=float('inf'))
    print(f'  wild creatures: guest {len(gc)}, host {len(hc)}; the guest\'s creature at {tuple(int(v) for v in near)}, '
          f'{dist(near, at):.0f} m from the vehicles; the host\'s nearest creature {off_host:.1f} m from it', flush=True)
    ck(off_host > 45, f'the host has no creature within 45 m of the guest\'s ({off_host:.1f} m)')
    _, hv, hst0 = civ_state(host)
    rej0 = stat(hst0, 'rejected')
    for i in ids:
        print(f'  guest: attack {i}:', guest.call(f'coop.civ attack {i} c{x:.1f},{y:.1f},{z:.1f}')['out'].strip(), flush=True)
    lo = re.search(r'last_order=(-?\d+)/(\d+)/(\d+)', guest.call('coop.civ tables')['out'])
    print('  guest last order (type/target kind/index):', lo and lo.group(0), flush=True)
    ck(lo and lo.group(1) == '1' and lo.group(2) == '3', f'the guest\'s game ordered an attack of an object ({lo and lo.group(0)})')
    start_d = {i: dist(hv[i]['pos'], near) for i in ids if i in hv}
    _, gv0, _ = civ_state(guest)
    gstart = {i: dist(gv0[i]['pos'], near) for i in ids if i in gv0}
    t0, closer, orders = time.time(), {}, {}
    while time.time() - t0 < 40:
        _, hv2, _ = civ_state(host)
        orders = {i: hv2[i]['order'] for i in ids if i in hv2}
        closer = {i: start_d[i] - dist(hv2[i]['pos'], near) for i in start_d if i in hv2}
        if closer and all(v > 15 or dist(hv2[i]['pos'], near) < 15 for i, v in closer.items()):
            break
        time.sleep(1)
    _, _, hst = civ_state(host)
    rej = stat(hst, 'rejected') - rej0
    print(f'  host: orders {orders}, metres closer to the place {({i: round(v) for i, v in closer.items()})} '
          f'(from {({i: round(v) for i, v in start_d.items()})}), rejected {rej}; {hst.split("siege_npc")[-1].strip()}',
          flush=True)
    ck(rej == 0, f'the host rejected none of the guest\'s orders ({rej})')
    ck(orders and all(o >= 0 for o in orders.values()), f'the host\'s vehicles took an order ({orders})')
    ck(closer and all(v > 15 for v in closer.values()),
       f'the host\'s vehicles drive to where the guest\'s creature is ({closer})')
    _, gv2, _ = civ_state(guest)
    gcloser = {i: round(gstart[i] - dist(gv2[i]['pos'], near)) for i in gstart if i in gv2}
    ck(gcloser and all(v > 10 for v in gcloser.values()),
       f'the guest sees its vehicles drive there too (metres closer on the guest: {gcloser})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
