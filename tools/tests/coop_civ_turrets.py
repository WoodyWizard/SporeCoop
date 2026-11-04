#!/usr/bin/env python3
"""Civilization: a city's defence turrets are the same in both games (BUG-0484).

The divergence detector had `noun_only_guest:turret` (a turret in the guest's game only) and `noun_only_host:turret`:
a city's turrets are made by the city growth of each game on its own (City_AddTurret 0xBE39A0 from 0xBE5890, paid
from the civilization's budget) and the snapshot carried none.

1. Two games on civ_npc; the turrets of every city in both games (`coop.civ turrets <city>`).
2. The guest's game gets a large growth budget for an NPC city's civilization (`coop.civ budget <city> 100000`
   on the guest only) and the city counts as attacked there (`coop.civ budget <city> attacked`, every 4 s): its
   own city growth may buy turrets with it (not seen within the test in the lab: the budget is spent, the growth
   bought no turret with the switch off either, lab 20261104-100015; the guest-growth half stays a guard).
3. The host's game adds one turret to another NPC city (`coop.civ addturret <city>`: the game's City_AddTurret).
4. 25 s later, the turrets of both cities in both games.
PASS: the guest's city of step 2 has the host's turrets (no turret of its own growth), the host's new turret of
step 3 stands in the guest's game too (each host turret has a guest turret within 6 m and the counts match).
SPORE_FIX_OFF=civ.turrets: the host's new turret is missing on the guest (FAIL).

  SPORE_LANE=9 python3 tools/tests/coop_civ_turrets.py
"""
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start


def turrets(con, ci):
    return [tuple(float(x) for x in m.groups()) for m in
            re.finditer(r'turret \S+ pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', con.call(f'coop.civ turrets {ci}')['out'])]


def same(h, g):
    if len(h) != len(g):
        return False
    left = list(g)
    for p in h:
        m = next((q for q in left if math.dist(p, q) < 6), None)
        if m is None:
            return False
        left.remove(m)
    return True


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    cities, _, _ = civ_state(host)
    npc = sorted(c for c, d in cities.items() if not d['player'] and d['owner'] < 0)
    ck(len(npc) >= 2, f'there are two NPC cities ({npc})')
    if len(npc) < 2:
        return ck.result()
    grow, add = npc[0], npc[1]
    for c in sorted(cities):
        print(f'  city {c}: turrets host {len(turrets(host, c))} guest {len(turrets(guest, c))}', flush=True)
    print('  guest budget:', guest.call(f'coop.civ budget {grow} 100000')['out'].strip(), flush=True)
    h_add0 = turrets(host, add)
    print('  host addturret:', host.call(f'coop.civ addturret {add}')['out'].strip(), flush=True)
    for _ in range(6):  # the growth buys turrets first for a city attacked in the last 20 s (0xC00600)
        print('  guest:', guest.call(f'coop.civ budget {grow} attacked')['out'].strip(), flush=True)
        time.sleep(4)
    time.sleep(3)
    for c in (grow, add):
        h, g = turrets(host, c), turrets(guest, c)
        print(f'  city {c}: host {len(h)} {h}', flush=True)
        print(f'  city {c}: guest {len(g)} {g}', flush=True)
    print('  guest budget now:', guest.call(f'coop.civ budget {grow}')['out'].strip(), flush=True)
    print('  guest', next((ln for ln in guest.call('coop.civ')['out'].splitlines() if ln.startswith('turrets:')), ''),
          flush=True)
    h_add = turrets(host, add)
    ck(len(h_add) == len(h_add0) + 1, f'the host\'s game added a turret to city {add} ({len(h_add0)} -> {len(h_add)})')
    ck(same(turrets(host, grow), turrets(guest, grow)),
       f'city {grow}: the guest\'s game has the host\'s turrets, none of its own growth')
    ck(same(h_add, turrets(guest, add)), f'city {add}: the host\'s new turret stands in the guest\'s game too')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
