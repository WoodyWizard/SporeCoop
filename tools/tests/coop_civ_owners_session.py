#!/usr/bin/env python3
"""Civilization over two evenings: the players' cities and vehicles keep their owners in the next session (E1 of
0.7.15, BUG-1153).

The mod's owner table (whose city, whose vehicle) lived only in the host's memory: every new session dealt the cities
anew in turns, and vehicles went to the owner of the nearest city, so a city the guest bought or captured and the
guest's army were the host's or another player's the next evening. The host now keeps a claim per city and vehicle
(its owner's stable key, by position) in the per-player store next to its save, and gives them back after the load;
while the owner is not there yet the host holds them for it.

1. Two games on civ_npc. The host buys the NPC city nearest its capital, the guest the next one (`coop.civ buycity`),
   and a vehicle there (`coop.civ buy`). On both games each bought city is the player civilization's and its buyer's. The owners of every player city and
   vehicle are noted (by position).
2. A new evening (twoinst.session_restart): the host saves, both quit, both start again, the host loads its save, the
   guest joins.
3. On both games every player city (by its position) has its owner of step 1; every vehicle the guest owned is the
   guest's again on the host (its owner table, which decides who may order it).
SPORE_FIX_OFF=store.civ_owners: the cities are dealt anew and the guest's vehicles stay the host's (FAIL).

  SPORE_LANE=10 python3 tools/tests/coop_civ_owners_session.py
"""
import time

from coop_civ import civ, dist
from coop_civ_owners_reload import by_pos, wait
from play_civ import frame_city
from twoinst import Checks, keyframe, session_restart, start


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and len(npc) >= 2, f'a player city {cap} and two NPC cities {npc}'):
        return ck.result()
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    for city, con, who in [(npc[0], host, 0), (npc[1], guest, 1)]:
        pos = hc[city]['pos']
        print(f'  player {who} buys city {city}:', con.call(f'coop.civ buycity {city} 500')['out'].strip(), flush=True)
        got = wait(lambda: all((c := by_pos(civ(x)[0], pos)) and c['player'] and c['owner'] == who for x in (host, guest)), 20)
        ck(got, f'city {city} is the player civilization\'s and player {who}\'s on both games')
    # the guest buys a vehicle at its city (the city menu's button: the host's game builds it, the guest's)
    print('  guest buys a vehicle:', guest.call(f'coop.civ buy {npc[1]} 0')['out'].strip()[:120], flush=True)
    wait(lambda: any(v['owner'] == 1 for v in civ(host)[1].values()), 20)
    time.sleep(4)  # it drives off its city and stands
    hc, hv = civ(host)
    want = {c['pos']: c['owner'] for c in hc.values() if c['player']}
    gveh = [v['pos'] for v in hv.values() if v['owner'] == 1]
    print('  before (city pos: owner):', {tuple(round(x) for x in p): o for p, o in want.items()},
          f'; the guest owns {len(gveh)} vehicles', flush=True)
    print('  host', next((ln for ln in host.call('coop.civ tables')['out'].splitlines() if ln.startswith('claims:')), '-'),
          flush=True)
    ck(gveh, 'the guest owns vehicles')
    gpos = hc[npc[1]]['pos']
    frame_city({'host': host, 'guest': guest}, gpos)
    keyframe('bought', "the guest's bought city in the player colours on both screens")

    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    time.sleep(8)
    hc, hv = civ(host)
    gc, _ = civ(guest)
    print('  host', next((ln for ln in host.call('coop.civ tables')['out'].splitlines() if ln.startswith('claims:')), '-'),
          flush=True)
    for pos, who in want.items():
        h, g = by_pos(hc, pos), by_pos(gc, pos)
        print(f'  city at {tuple(round(x) for x in pos)}: before {who}, host {h and (h["player"], h["owner"])}, '
              f'guest {g and (g["player"], g["owner"])}', flush=True)
        ck(h and h['owner'] == who and g and g['owner'] == who,
           f'next session: the city at {tuple(round(x) for x in pos)} is still player {who}\'s on both games '
           f'(host {h and h["owner"]}, guest {g and g["owner"]})')
    back = [p for p in gveh if any(v['owner'] == 1 and dist(v['pos'], p) < 6.0 for v in hv.values())]
    print(f'  next session: {len(back)} of the guest\'s {len(gveh)} vehicles are its own again', flush=True)
    ck(gveh and len(back) == len(gveh), f'next session: the guest\'s vehicles are its own again ({len(back)}/{len(gveh)})')
    frame_city({'host': host, 'guest': guest}, gpos)
    keyframe('next_session', "next session: the guest's bought city in the same colours on both screens")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
