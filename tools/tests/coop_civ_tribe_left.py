#!/usr/bin/env python3
"""Civilization: no Tribe stage village appears in a guest's Civ world (BUG-0539).

The detector saw whole tribe villages (tribes, huts, tools, food mats, bundles; up to 11 tribes) in the guest's Civ
world only. The host's NPC vehicles raid the old villages (a behaviour tree action: money, then DestroyInstance of
the tribe, 0xDCB2B2); the guest's mirrors run no behaviour tree, so its villages stood for good.

1. Two games on civ_npc: the Tribe stage objects in each game's noun list.
2. The host's civilization captures an NPC city (`coop.civ capture <city>`; a capture alone removes nothing).
3. The host's game destroys two tribes as its raid does (`coop.civ razetribe <i>`: DestroyInstance of the tribe).
4. 15 s later, the counts and places of both games.
PASS: the guest's game has no more tribe objects than the host's after each step, and none where the host's two
are gone.
SPORE_FIX_OFF=civ.tribe_ruins: the guest keeps the two villages (FAIL).

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
    # bundles are dropped by fights and captures in each game on its own too (34 / 35 in a warm game, lab
    # 20261104-131223): the village's own objects are compared, bundles printed (BUG-0548)
    VILLAGE = [k for k in NOUNS.values() if k != 'game_bundle']
    ck(all(g0[k] <= h0[k] for k in VILLAGE), f'before the capture the guest has no tribe objects of its own')
    # a capture drops bundles (the loot) in each game on its own (36 / 37 in lab 20261104-130500): not compared here
    ck(all(g1[k] <= h1[k] for k in VILLAGE),
       f'after the capture the guest has no tribe objects of its own')
    for i in (0, 0):
        print('  host razetribe:', host.call(f'coop.civ razetribe {i}')['out'].strip(), flush=True)
        time.sleep(1)
    time.sleep(15)
    h2, g2 = tribe_objs(host), tribe_objs(guest)
    print(f'  after the raids: host {h2}', flush=True)
    print(f'  after the raids: guest {g2}', flush=True)
    print('  guest', next((ln for ln in guest.call('coop.civ')['out'].splitlines() if 'tribe_ruins:' in ln), ''), flush=True)
    # (the host's own vehicles may raid one more meanwhile)
    ck(h2['tribe'] <= h1['tribe'] - 2, f"the host's game destroyed two tribes ({h1['tribe']} -> {h2['tribe']})")
    ck(all(g2[k] <= h2[k] for k in VILLAGE),
       f'after the raids the guest has no tribe objects the host does not have (host {h2}, guest {g2})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
