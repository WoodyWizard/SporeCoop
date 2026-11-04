#!/usr/bin/env python3
"""Civilization: no Tribe stage village appears in a guest's Civ world (BUG-0539).

The detector saw a whole tribe village (tribes, huts, tools, food mats, bundles; 11 tribes) in the guest's Civ
world only, from the moment a city was captured (play_civ, out/lab/20261104-110843: after "Bob captured a city").

1. Two games on civ_npc: the Tribe stage objects in each game's noun list.
2. The host's civilization captures an NPC city (`coop.civ capture <city>`, the game's capture).
3. 25 s later (the capture caption closes after 8 s), the same counts.
PASS: the guest's game has no more tribe objects than the host's, before and after.

  SPORE_LANE=6 python3 tools/tests/coop_civ_tribe_left.py
"""
import time

from play_civ import civ_state
from twoinst import Checks, start

NOUNS = {'18C6D19': 'tribe', '1E4DAAE': 'tribe_hut', '629BAFE': 'tribe_food_mat', '18C8F0C': 'tribe_tool',
         '18C431C': 'game_bundle'}


def tribe_objs(con):
    out = {}
    for n, name in NOUNS.items():
        out[name] = sum(1 for ln in con.call(f'coop.civ nouns {n}')['out'].splitlines() if ln.startswith('obj '))
    return out


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    h0, g0 = tribe_objs(host), tribe_objs(guest)
    print(f'  before: host {h0}', flush=True)
    print(f'  before: guest {g0}', flush=True)
    cities, _, _ = civ_state(host)
    npc = sorted(c for c, d in cities.items() if not d['player'] and d['owner'] < 0)
    ck(npc, f'there is an NPC city ({npc})')
    if not npc:
        return ck.result()
    print('  host:', host.call(f'coop.civ capture {npc[0]}')['out'].strip(), flush=True)
    seq = []
    for _ in range(10):
        time.sleep(2.5)
        h, g = tribe_objs(host), tribe_objs(guest)
        seq.append(f"{h['tribe']}/{g['tribe']}")
    print('  tribes host/guest every 2.5 s:', ' '.join(seq), flush=True)
    h1, g1 = tribe_objs(host), tribe_objs(guest)
    print(f'  after: host {h1}', flush=True)
    print(f'  after: guest {g1}', flush=True)
    ck(all(g0[k] <= h0[k] for k in NOUNS.values()), f'before the capture the guest has no tribe objects of its own')
    ck(all(g1[k] <= h1[k] for k in NOUNS.values()), f'after the capture the guest has no tribe objects of its own')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
