#!/usr/bin/env python3
"""Civilization: a mirror vehicle removed between a guest's save and its end stays out of the civ lists (BUG-9301,
audit CIV-1).

While a guest's save is written the mod takes the guest's mirror vehicles (the host's vehicles) out of every
civilization's vehicle (+0xB0) and selectable (+0xD4) vector (BUG-0470) and puts them back at the save's end, which
runs on the next frame, after that frame's network receive. A host snapshot in that frame that no longer names a mirror
(the host's vehicle died or was disbanded during the save hitch) removes it (Vehicle_Remove: out of the game's list at
once, freed a moment later), and the save's end put the removed vehicle back: a dangling pointer in the game's lists.

1. Two games on civ_npc. The host buys vehicles at its city (`coop.civ buy`): the guest shows mirrors.
2. The guest arms `coop.civ savekill on` (tests: its next save's end first removes one taken mirror, as that host
   snapshot would) and saves with the game's own save (`coop.save`).
3. PASS: the save's end ran with the removal (`coop.civ tables` save_took: armed=0, gone >= 1), no entry of any
   civilization's vehicle lists on the guest is a vehicle the game's list no longer has (`coop.civ civlists` unlisted=0,
   the game's own vectors), and both games run without a fault for 15 s.
SPORE_FIX_OFF=civ.save_took_listed must FAIL (the removed vehicle is back in the lists: unlisted >= 1, or a fault).

  SPORE_LANE=3 python3 tools/tests/coop_civ_save_took.py
"""
import re
import time

from coop_creature_e84 import close_boxes
from play_civ import civ_state
from twoinst import Checks, save_taken, start


def took(con):
    m = re.search(r'save_took: fix=(\d+) taken=(\d+) back=(\d+) gone=(\d+) cleared=(\d+) armed=(\d+)',
                  con.call('coop.civ tables')['out'])
    return dict(zip(('fix', 'taken', 'back', 'gone', 'cleared', 'armed'), map(int, m.groups()))) if m else {}


def civlists(con):
    out = con.call('coop.civ civlists')['out']
    rows = [tuple(map(int, m)) for m in re.findall(r'vehicles=(\d+) nulls=\d+ selectable=(\d+) nulls=\d+ unlisted=(\d+)', out)]
    return sum(r[0] for r in rows), sum(r[2] for r in rows), out


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    hc, _, _ = civ_state(host)
    mine = sorted(i for i, c in hc.items() if c['player'])
    ck(mine, f'the host has a player city ({mine})')
    if not mine:
        return ck.result()
    for _ in range(3):
        print('  host:', host.call(f'coop.civ buy {mine[0]} 0')['out'].strip()[:100], flush=True)
        time.sleep(2)
    time.sleep(4)
    _, gv, _ = civ_state(guest)
    _, hv, _ = civ_state(host)
    print(f'  vehicles: host {len(hv)}, guest {len(gv)}', flush=True)
    ck(len(gv) >= 2, f'the guest shows the host\'s vehicles as mirrors ({len(gv)} of {len(hv)})')
    n0, un0, _ = civlists(guest)
    print(f'  guest civ lists before: {n0} vehicles, {un0} unlisted', flush=True)
    ck(un0 == 0, f'before the save no civ list entry on the guest is a vehicle the game no longer has ({un0})')
    print('  guest:', guest.call('coop.civ savekill on')['out'].strip(), flush=True)
    said, _, _ = save_taken(guest, 30, 'guest save')
    ck(said.startswith('saved (mode'), f'the guest saves its game ({said[:80]})')
    close_boxes(guest, 10)
    time.sleep(2)
    t = took(guest)
    n1, un1, out = civlists(guest)
    print(f'  guest save_took {t}; civ lists after: {n1} vehicles, {un1} unlisted', flush=True)
    print('   ', ' | '.join(out.strip().splitlines())[:400], flush=True)
    ck(t.get('armed') == 0 and t.get('back', 0) + t.get('gone', 0) >= 1,
       f'the save took mirrors out and its end ran with one of them removed first ({t})')
    ck(un1 == 0, f'no civ list entry on the guest is a vehicle the game removed (unlisted {un1})')
    ck(n1 >= n0 - 1, f'the other mirrors are back in the lists ({n0} -> {n1})')
    t0 = time.time()
    while time.time() - t0 < 15:
        time.sleep(3)
        civlists(guest)  # (the lists walked again: a freed entry read here as by the civ update)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
