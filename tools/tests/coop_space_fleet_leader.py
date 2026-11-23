#!/usr/bin/env python3
"""BUG-1288 (WP-S1 of 0.7.17, switch fleet.leader): the NPC ships of a star system without the host, three players.

Alice hosts at her home star A; Carol (the second guest) jumps to a free star B first, Bob (the first guest, the lower
id) after her (tools/tests/party.py: Carol is instance 20+N).

1. leader  the host names Carol the leader of B: she was there first, Bob's lower id does not take the lead (sticky,
           squadron_logic.h step_leads); Bob's game mirrors her (`coop.fleet list` status: space leader role=...), the
           host's table has the row (space lead <world> <Carol's id>).
2. ships   Carol's game gets three pirates (`coop.fleet spawn 2`): Bob's game shows them under the same ship ids at
           the same places (the game's UFO list on both machines), and Bob's own NPC ships there are gone (his list has
           no ship of the game's own besides his ship, the puppets and his escort).
3. hit     Bob shoots one of the mirrors (`coop.fleet shoot <id> 40`, through his TakeDamage hook): the ship in
           Carol's game loses hit points (the game's own hp, +0x540).
4. damage  one of Carol's pirates hits Bob's ship as Carol's game has it (`coop.fleet hurt <id> <Bob>`): Bob's own ship
           in his game loses hit points.
5. host    Alice jumps to B: both guests mirror the host again (0.7.16), the host's table is empty, nothing crashes.

Key frames: Bob and Carol at B with the pirates (the same ships on both screens), and after the host arrived.
SPORE_FIX_OFF=fleet.leader must FAIL (1 and 2: nobody leads, Bob keeps his own ships).

  SPORE_LANE=5 python3 tools/tests/coop_space_fleet_leader.py [--players 3]   (3 only: the lab counts its games by it)
"""
import argparse
import math
import re
import time

import twoinst
from coop_space_jump import place, settle, sp
from harness import waiter
from party import Party
from twoinst import Checks

SHIP = re.compile(r'ship #(-?\d+) (\S+) type=(\d+) pol=(\S+) hp=(-?[\d.]+)/(-?[\d.]+)( \(me\))? '
                  r'pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) inv=(\d) model=\S+( puppet)?')
wait = waiter(step=0.5)


def fleet(x):
    """`coop.fleet list` of a console (twoinst) or a party Player"""
    r = x.call('coop.fleet list')
    return r.get('out', '') if isinstance(r, dict) else r


def ships(x):
    """the game's UFO list of that machine: [{id, ptr, type, hp, me, pos, inv, puppet}]"""
    out = fleet(x)
    return [{'id': int(m.group(1)), 'ptr': m.group(2), 'type': int(m.group(3)), 'hp': float(m.group(5)),
             'me': bool(m.group(7)), 'pos': (float(m.group(8)), float(m.group(9)), float(m.group(10))),
             'inv': int(m.group(11)), 'puppet': bool(m.group(12))} for m in SHIP.finditer(out)]


def leader_line(x):
    m = re.search(r'space leader: fix=(\d) role=(\w+) leader=(-?\d+) place=([0-9A-F]{8})', fleet(x))
    return (m.group(2), int(m.group(3)), int(m.group(4), 16)) if m else ('?', -2, 0)


def leads(x):
    return {int(w, 16): int(l) for w, l in re.findall(r'^space lead ([0-9A-F]{8}) (\d+)', fleet(x), re.M)}


def npc_ids(x):
    return {s['id']: s for s in ships(x) if s['id'] >= 0}


def own_npcs(x):
    """NPC ships of that game's own: not mirrored or led (id -1), not its ship, a puppet or its escort (type 3)"""
    return [s for s in ships(x) if s['id'] < 0 and not s['me'] and not s['puppet'] and s['type'] not in (0, 3)]


def me(x):
    return next((s for s in ships(x) if s['me']), None)


def star(x):
    return place(x)[1]


def jump(ck, g, target):
    settle(g.con, g.name, 30)
    print(f'  {g.name} jumpto {target}:', sp(g.con, 'jumpto', target), flush=True)
    ok = wait(lambda: star(g.con) == target, 60, 1)
    settle(g.con, g.name, 30)
    ck(ok and place(g.con)[0] == (target | 0x80000000), f'{g.name} is in the system of star {target:08X} '
       f'({place(g.con)[0]:08X})')
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--players', type=int, default=3, choices=[3])
    ap.parse_args()
    ck = Checks()
    p = Party.start('space', 3)
    try:
        p.ids()
        alice, bob, carol = p.host, p.guests[0], p.guests[1]
        print(f'  ids: Bob={bob.pid} Carol={carol.pid}', flush=True)
        if not p.check_together(ck, 'join'):
            return ck.result()
        home = star(alice.con)
        free = [int(x) for x in re.findall(r'free (\d+)', sp(carol.con, 'allies free')) if int(x) != home]
        if not ck(free, f'a free star near the home star {home:08X} ({free[:4]})'):
            return ck.result()
        b = free[0]
        world_b = b | 0x80000000
        for g in (bob, carol):
            print(f'  {g.name} drive:', sp(g.con, 'tool add interstellardrive'), flush=True)

        # 1. Carol first, then Bob: Carol leads although Bob has the lower id
        if not jump(ck, carol, b):
            return ck.result()
        time.sleep(3)
        if not jump(ck, bob, b):
            return ck.result()
        ok = wait(lambda: leads(alice).get(world_b) == carol.pid, 15)
        ck(ok, f'[leader] the host names Carol the leader of {world_b:08X} ({leads(alice)})')
        ok = wait(lambda: leader_line(carol.con)[:2] == ('leader', carol.pid) and
                  leader_line(bob.con)[:2] == ('mirror', carol.pid), 15)
        ck(ok, f'[leader] Carol leads, Bob mirrors her (sticky: Bob came later): Carol {leader_line(carol.con)}, '
           f'Bob {leader_line(bob.con)}')

        # 2. three pirates in Carol's game: Bob's game has them under the same ids, at the same places
        for _ in range(3):
            print('  Carol:', carol.call('coop.fleet spawn 2').strip(), flush=True)
        time.sleep(1)
        lead_ids = set(npc_ids(carol.con))
        ok = wait(lambda: lead_ids and lead_ids <= set(npc_ids(bob.con)), 10)
        ck(ok, f'[ships] Bob\'s game shows Carol\'s NPC ships under her ids ({sorted(lead_ids)} / '
               f'{sorted(npc_ids(bob.con))})')
        far = {}
        for _ in range(6):  # the closest of a few samples (both lists are read one after the other)
            c, g = npc_ids(carol.con), npc_ids(bob.con)
            for i in lead_ids & set(c) & set(g):
                far[i] = min(far.get(i, 1e9), math.dist(c[i]['pos'], g[i]['pos']))
            time.sleep(0.3)
        ck(far and max(far.values()) < 20, f'[ships] each one where Carol\'s game has it ({ {i: round(d, 1) for i, d in far.items()} })')
        ck(wait(lambda: not own_npcs(bob.con), 10), f'[ships] Bob\'s own NPC ships at B are gone ({own_npcs(bob.con)[:3]})')
        twoinst.keyframe('fleet_leader_b', 'Bob and Carol at the free star B: the same three pirate ships next to Carol\'s '
                         'ship on both screens', players={'bob': bob.con, 'carol': carol.con})

        # 3. Bob's hit on a mirror lands on Carol's ship
        c = npc_ids(carol.con)
        target = next((i for i in sorted(c) if c[i]['hp'] > 60 and i in npc_ids(bob.con)), None)
        if ck(target is not None, f'[hit] a ship with hit points to lose ({c})'):
            hp0 = c[target]['hp']
            print(f'  Bob shoot #{target}:', bob.call(f'coop.fleet shoot {target} 40').strip(), flush=True)
            ok = wait(lambda: npc_ids(carol.con).get(target, {'hp': 0})['hp'] <= hp0 - 20, 8)
            ck(ok, f'[hit] ship #{target} in Carol\'s game lost hit points ({hp0} -> '
                   f'{npc_ids(carol.con).get(target, {}).get("hp")})')

        # 4. Carol's pirate hits Bob's ship as her game has it: Bob's own ship takes it
        c = npc_ids(carol.con)
        att = next((i for i in sorted(c) if i != target), next(iter(c), None))
        mine = me(bob.con)
        if ck(att is not None and mine, f'[damage] an attacker and Bob\'s ship ({att}, {mine})'):
            hp0 = mine['hp']
            print(f'  Carol hurt #{att} -> Bob:', carol.call(f'coop.fleet hurt {att} {bob.pid} 8').strip(), flush=True)
            ok = wait(lambda: (me(bob.con) or {'hp': hp0})['hp'] < hp0 - 1, 8)
            ck(ok, f'[damage] Bob\'s own ship lost hit points ({hp0} -> {(me(bob.con) or {}).get("hp")})')

        # 5. the host arrives: everybody mirrors the host again
        if jump(ck, alice, b):
            ok = wait(lambda: not leads(alice) and all(leader_line(g.con)[:2] == ('mirror', 0) for g in (bob, carol)), 20)
            ck(ok, f'[host] with the host at B both guests mirror the host, no guest leads ({leads(alice)}, '
                   f'Bob {leader_line(bob.con)}, Carol {leader_line(carol.con)})')
            twoinst.keyframe('fleet_leader_host', 'Alice arrived at B: all three ships together, the host\'s NPC ships '
                             'on every screen', players={'alice': alice.con, 'bob': bob.con, 'carol': carol.con})
        time.sleep(2)
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
