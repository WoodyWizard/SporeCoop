#!/usr/bin/env python3
"""Civilization: a city the host's player took, the other nations' feelings and money are the host's in the
guest's game (BUG-0593, wsync rows city_owner, civ_rel, civ_money; mods/coop/wsync.cpp).

1. The host's civilization captures an NPC city (`coop.civ capture <city>`: the game's Capture, its caption and
   cinematic, then ChangeOwner, which gives the city's buildings the player's political id).
2. The host's game changes another nation's feelings about the player (the game's ApplyRelationship, as a capture's
   or a gift's event does) and that nation's money (cCivilization +0x98, what its trade and taxes change).
3. 25 s later the guest's own game (`coop.wstate`: every building's and turret's political id, CalculateRelationship
   of every nation, +0x98 of every nation) is compared with the host's (worlddiff.diff_state): no
   planet_owner:building_* row, no state:relation, no state:civ_money row. The host's game itself must show the
   change (the captured city's buildings are the player's, the relation moved by 3 or more, the money by 1000).

SPORE_FIX_OFF=wsync.city_owner,wsync.civ_rel,wsync.civ_money: the guest keeps the NPC's buildings, relation and
money: must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_wsync_civ.py
"""
import time

import worlddiff
from coop_civ import civ
from twoinst import Checks, start


def states(host, guest):
    return worlddiff.state_dump(host), worlddiff.state_dump(guest)


def buildings_near(st, pos, r=60.0):
    return [x for x in st.planet if x['noun'] in (0x018EA1EB, 0x018EB106, 0x018EA2CC, 0x01A56ABA, 0x0436F342)
            and x['pos'] and worlddiff.math.dist(x['pos'], pos) < r]


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(6)
    hc, _ = civ(host)
    ppol = next((v['pol'] for v in hc.values() if v['player']), None)
    npc = [k for k, v in hc.items() if not v['player']]
    print('  player pol', ppol, 'npc cities', {k: hc[k]['pol'] for k in npc}, flush=True)
    if not ck(ppol and npc, 'an NPC city to capture'):
        return ck.result()
    city = npc[0]
    cpos, cpol = hc[city]['pos'], hc[city]['pol']
    hs0, gs0 = states(host, guest)
    civs = [c for c in hs0.civs if not c['player']]
    other = next((c for c in civs if c['pol'] != cpol), civs[0] if civs else None)
    if not ck(other is not None, 'another nation in the host\'s game'):
        return ck.result()
    print(f'  before: nation {other["pol"]} rel {other["rel"]:.2f} money {other["money"]:.0f} (host)', flush=True)
    print(' ', host.call(f'coop.civ capture {city}')['out'].strip(), flush=True)
    print(' ', host.call(f'coop.wsync rel {other["pol"]} -4')['out'].strip(), flush=True)
    print(' ', host.call(f'coop.wsync civmoney {other["pol"]} 1500')['out'].strip(), flush=True)
    # the money first: the nation's own AI spends it within seconds (lab 20261105-122514: 1500 -> 0 in 25 s)
    time.sleep(3)
    hm, gm = states(host, guest)
    h0 = next((c for c in hm.civs if c['pol'] == other['pol']), None)
    g0 = next((c for c in gm.civs if c['pol'] == other['pol']), None)
    print(f'  3 s later: nation {other["pol"]} money host {h0 and h0["money"]}, guest {g0 and g0["money"]}', flush=True)
    ck(h0 and abs(h0['money'] - other['money']) >= 1000, 'the host\'s game: the nation\'s money changed')
    ck(g0 and h0 and abs(g0['money'] - h0['money']) < 50, 'the guest\'s game: the nation has the host\'s money (3 s)')
    time.sleep(22)
    hc, _ = civ(host)
    gc, _ = civ(guest)
    ck(hc.get(city, {}).get('pol') == ppol and gc.get(city, {}).get('pol') == ppol,
       f'the city belongs to the player civilization on both machines (host {hc.get(city, {}).get("pol")}, '
       f'guest {gc.get(city, {}).get("pol")})')
    hs, gs = states(host, guest)
    hb, gb = buildings_near(hs, cpos), buildings_near(gs, cpos)
    print(f'  the city\'s buildings: host {[b["pol"] for b in hb]}, guest {[b["pol"] for b in gb]}', flush=True)
    ck(hb and all(b['pol'] == ppol for b in hb), 'the host\'s game: the captured city\'s buildings are the player\'s')
    ck(gb and all(b['pol'] == ppol for b in gb), 'the guest\'s game: the captured city\'s buildings are the player\'s')
    h1 = next((c for c in hs.civs if c['pol'] == other['pol']), None)
    g1 = next((c for c in gs.civs if c['pol'] == other['pol']), None)
    print(f'  after: nation {other["pol"]} host rel {h1 and h1["rel"]:.2f} money {h1 and h1["money"]:.0f}, '
          f'guest rel {g1 and g1["rel"]:.2f} money {g1 and g1["money"]:.0f}', flush=True)
    ck(h1 and abs(h1['rel'] - other['rel']) >= 3, 'the host\'s game: the nation\'s relation changed')
    ck(g1 and abs(g1['rel'] - h1['rel']) < 0.5, 'the guest\'s game: the nation feels about the player as the host\'s')
    ck(g1 and h1 and abs(g1['money'] - h1['money']) < 50, 'the guest\'s game: the nation has the host\'s money (25 s)')
    rows = [(w, k, t) for w, k, _, t in worlddiff.diff_state(hs, gs, 'Civilization')
            if 'planet_owner' in w or k in ('relation', 'civ_money')]
    for r in rows:
        print('  detector:', r, flush=True)
    ck(not rows, f'the detector has no owner / relation / money row ({len(rows)})')
    print('  guest: ' + guest.call('coop.wsync')['out'].strip().replace('\n', ' | '), flush=True)
    host.call('key enter')  # a caption still waiting (fix off) does not hold the next test
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
