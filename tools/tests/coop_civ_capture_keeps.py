#!/usr/bin/env python3
"""Civilization: a city the host captures stays the host's when the shared capital stops being shared (BUG-0875).

Discord 2026-11-09 ("their settlement is captured, the host's is not"): with 2 players and 1 city (the shared
capital) the second city crosses the threshold `cities < players`, and host_assign_cities dealt every city out
again in turns: the capital to the host, the captured city to the guest, a tick after the host took it.

1. Two games on civ_npc (the player civilization has its capital only, shared by both players).
2. The host's own capture of the nearest NPC city through the game's capture meter (`coop.civ siege <city> 0 <hits>
   pc`: capture damage by player 0, the game's CheckMilitaryCapture takes the city at 100).
3. For 12 s, on both games: the city's political id is the player civilization's and its owner is the host (0);
   the guest's game refuses the guest a purchase there ("this city belongs to player 0") and the guest can still
   buy at the capital.
   SPORE_FIX_OFF=civ.capture_keeps: the city becomes the guest's (owner 1): FAIL.
"""
import time

from coop_civ import civ, dist
from twoinst import Checks, keyframe, start


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(len(cap) == 1 and npc, f'one player city {cap} and NPC cities {npc}'):
        return ck.result()
    print('  host cities', {k: (v['owner'], v['player']) for k, v in hc.items()}, flush=True)
    tgt = min(npc, key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    print('  host:', host.call(f'coop.civ siege {tgt} 0 150 pc')['out'].strip(), flush=True)
    seen_h, seen_g = [], []
    t0 = time.time()
    while time.time() - t0 < 12:
        h, _ = civ(host)
        g, _ = civ(guest)
        if tgt in h:
            seen_h.append((h[tgt]['player'], h[tgt]['owner']))
        if tgt in g:
            seen_g.append((g[tgt]['player'], g[tgt]['owner']))
        time.sleep(1)
    print(f'  city {tgt} host (player, owner): {seen_h}', flush=True)
    print(f'  city {tgt} guest (player, owner): {seen_g}', flush=True)
    print('  host', host.call('coop.civ tables')['out'].strip().splitlines()[-1], flush=True)
    ck(seen_h and seen_h[-1][0], f"the host's game captured city {tgt} for the player civilization")
    ck(seen_g and seen_g[-1][0], f"the guest's game shows city {tgt} as the player civilization's")
    ck(seen_h and all(o == 0 for p, o in seen_h if p), f'the captured city stays the host\'s in the host\'s game ({seen_h[-3:]})')
    ck(seen_g and all(o == 0 for p, o in seen_g if p), f'the captured city is the host\'s in the guest\'s game ({seen_g[-3:]})')
    r = guest.call(f'coop.civ buy {tgt} 0')['out'].strip()
    print('  guest buys at the captured city:', r, flush=True)
    ck('belongs to player 0' in r, f"the guest's game refuses the guest a purchase in the host's city ({r})")
    g, _ = civ(guest)
    print('  guest cities', {k: (v['owner'], v['player']) for k, v in g.items()}, flush=True)
    ck(cap[0] in g and g[cap[0]]['owner'] in (253, 1), f"the guest still has a city to play from (capital owner {g.get(cap[0], {}).get('owner')})")
    keyframe('captured', "the captured city with the host's colours on both screens")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
