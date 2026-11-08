#!/usr/bin/env python3
"""Civilization: a player who takes over the id of a player who left does not inherit the old role in the host's
game (BUG-0064), checked on what the role does in the game: a military governor's vehicles hit 25 % harder on the
host (civ.cpp b_damage). coop_civ_shared.py checks the role tables; this test the damage (BUG-0430: the game's own
state, not the mod's bookkeeping).

1. civ_npc. The host spawns two military land vehicles and gives them to the guest (`coop.give 1`, as
   coop_civ_guest_attack.py) and orders them to attack the nearest NPC city (`coop.civ attack <id> <city> 2`, the
   game's own SetOrder).
2. Three phases of PHASE s each; in each the host's game's building hit points of that city (`coop.civ buildings`)
   are read 5 times a second and every drop of one building between two reads is one hit's damage (two hits in one
   read are rare: the lower quartile of the drops is the phase's damage per hit):
     A  the guest has no role                                  (the baseline)
     M  the guest picks military (`coop.role 1`)               must hit >= 1.15 x A: the check sees the role
     B  the guest leaves, joins again (the same id), gets the vehicles again, picks no role
                                                               must hit <= 1.10 x A
PASS: M sees the role and B does not keep it. FAIL with SPORE_FIX_OFF=role.fix (the host keeps the old player's
military role for the newcomer: B ~ 1.25 x A).

  SPORE_LANE=4 python3 tools/tests/coop_civ_role_damage.py
"""
import re
import time

import lane
from play_civ import civ_state, dist
from twoinst import Checks, start, wait_guest_in

PHASE = 30


def owners(con, ids):
    out = {}
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) .* owner=(-?\d+)', ln)
        if m and int(m.group(1)) in ids:
            out[int(m.group(1))] = int(m.group(2))
    return out


def building_hp(con, ci):
    return {m.group(1): float(m.group(2)) for m in
            re.finditer(r'building (\S+) noun=\S+ state=-?\d+ hp=([-\d.]+)', con.call(f'coop.civ buildings {ci}')['out'])}


def give(host, guest, ids):
    """the owners of the vehicles in the host's game, where the damage is decided (after a rejoin the guest's own
    list took longer than 3 s to show them again: lab 20261108-074515); the guest's view is waited for and printed"""
    for i in ids:
        host.call(f'coop.civ select {i} 1')
    print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(1)
        h, g = owners(host, ids), owners(guest, ids)
        if all(h.get(i) == 1 for i in ids) and all(g.get(i) == 1 for i in ids):
            break
    print(f'  owners: host {h}, guest {g} ({time.time() - t0:.0f} s)', flush=True)
    return h


def phase(host, ids, tgt, name):
    """the damage per hit on the host's buildings of city tgt over PHASE s (None: no hit seen)"""
    for i in ids:
        host.call(f'coop.civ attack {i} {tgt} 2')
    drops, last, t0 = [], building_hp(host, tgt), time.time()
    while time.time() - t0 < PHASE:
        time.sleep(0.2)
        now = building_hp(host, tgt)
        drops += [last[b] - hp for b, hp in now.items() if b in last and 0.5 < last[b] - hp]
        last = now
    drops.sort()
    per_hit = drops[len(drops) // 4] if len(drops) >= 4 else None
    print(f'  {name}: {len(drops)} hits on city {tgt}, damage per hit {per_hit} (drops {drops[:12]})', flush=True)
    return per_hit


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    print(' ', host.call('coop.role')['out'].strip().replace('\n', ' | '), flush=True)
    before = set(owners(host, range(1, 200)))
    for _ in range(2):
        print('  host:', host.call('coop.civ spawnveh 0 0 0')['out'].strip(), flush=True)
    time.sleep(2)
    ids = sorted(set(owners(host, range(1, 200))) - before)
    got = give(host, guest, ids)
    ck(len(ids) == 2 and all(got.get(i) == 1 for i in ids), f'the guest owns the two vehicles ({got})')
    cities, veh, _ = civ_state(host)
    mine = [veh[i]['pos'] for i in ids if i in veh]
    npc = [i for i, c in cities.items() if not c['player'] and c['buildings'] > 0]
    tgt = min(npc, key=lambda i: dist(cities[i]['pos'], mine[0])) if npc and mine else None
    ck(tgt is not None, f'an NPC city with buildings to attack ({tgt})')
    if tgt is None or len(ids) != 2:
        return ck.result()

    a = phase(host, ids, tgt, 'A (no role)')
    print(' ', guest.call('coop.role 1')['out'].strip().replace('\n', ' | '), flush=True)
    time.sleep(1.5)
    m = phase(host, ids, tgt, 'M (military)')
    ck(a and m and m >= 1.15 * a, f'the check sees a military role in the host\'s game (A {a}, M {m})')

    guest.call('coop.leave')
    time.sleep(2)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 150), 'the guest joins again')
    time.sleep(3)
    print('  roles after the rejoin: host', host.call('coop.role')['out'].strip().replace('\n', ' | '), flush=True)
    alive = [i for i in ids if i in owners(host, ids)]
    got = give(host, guest, alive) if alive else {}
    ck(alive and all(got.get(i) == 1 for i in alive), f'the guest owns its vehicles again ({got})')
    b = phase(host, alive or ids, tgt, 'B (rejoined, no role)')
    ck(a and b and b <= 1.10 * a, f'the newcomer\'s vehicles hit as a player without a role on the host (A {a}, B {b}; '
                                  f'1.25 x A = the old player\'s military role kept)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
