#!/usr/bin/env python3
"""Civilization: a ruin the host no longer has does not stay on the guest (BUG-0324).

play_civ.py's end audit found "city 1: 0 ruins on the host, 1 on the guest" in 3 of 5 runs: the
siege of an NPC city ruined buildings on both games; the host's game rebuilt one after the siege,
the guest kept the ruin (its reconciliation only made ruins, never undid one).

1. Two games on civ_npc.
2. The guest's game alone ruins one building of an NPC city (`coop.civ ruinlocal <city>`, the
   state a building has on the guest when the host rebuilt it).
3. For 20 s: that city's ruins and buildings on the guest's game against the host's (`coop.civ`
   city lines, the game's building states).
PASS: within 10 s the guest has the host's ruins and building count, and keeps them.
--nofix (`coop.fix civ.ruin_repair off`): must FAIL (the ruin stays).

  SPORE_LANE=7 python3 tools/tests/coop_civ_ruin_repair.py [--nofix]
"""
import argparse
import time

from play_civ import civ_state
from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix civ.ruin_repair off')['out'].strip(), flush=True)
    time.sleep(3)
    hc = civ_state(host)[0]
    npc = sorted(i for i, c in hc.items() if not c['player'] and c['buildings'] >= 2 and c['ruins'] == 0)
    ck(npc, f'an NPC city with buildings and no ruins ({npc})')
    if not npc:
        return ck.result()
    ci = npc[0]
    out = guest.call(f'coop.civ ruinlocal {ci}')['out'].strip()
    print('  guest:', out, flush=True)
    ck('ruined here' in out, 'the guest\'s game ruined one building of the city')
    first_same, last_diff, t0 = None, None, time.time()
    while time.time() - t0 < 20:
        h = civ_state(host)[0].get(ci, {})
        g = civ_state(guest)[0].get(ci, {})
        same = h.get('ruins') == g.get('ruins') and h.get('buildings') == g.get('buildings')
        t = time.time() - t0
        if same and first_same is None:
            first_same = t
        if not same:
            last_diff = t
            print(f'    {t:4.1f} s: host ruins {h.get("ruins")} buildings {h.get("buildings")}, '
                  f'guest ruins {g.get("ruins")} buildings {g.get("buildings")}', flush=True)
        time.sleep(1)
    ck(last_diff is None or last_diff < 10,
       f'the guest has the host\'s ruins and buildings again within 10 s (last difference at '
       f'{"-" if last_diff is None else f"{last_diff:.0f} s"})')
    print('  guest:', [ln for ln in civ_state(guest)[2].split() if ln.startswith('buildings_fixed')], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
