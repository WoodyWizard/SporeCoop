#!/usr/bin/env python3
"""Civilization: a guest's purchase of another nation's city changes hands in both games (BUG-0877).

The game's purchase (the talk's accept -> cCivilization::FinishBuyCity -> cCity buy: SpendMoney, then Capture
type 2) ran in the guest's game: the money went, Capture was refused on a guest and the protocol had no purchase,
so the city stayed the other nation's in both games. Now the guest's game sends CMD_BUYCITY and the host's game buys.

1. Two games on civ_npc. The guest's game buys the nearest NPC city for 500 through the game's purchase function
   (`coop.civ buycity <city> 500`: cCity buy, as FinishBuyCity calls it).
2. Within 15 s, on both games: the city's political id is the player civilization's, its owner the guest (1).
   SPORE_FIX_OFF=civ.buy_city: the city stays the NPC's in both games: FAIL.
3. BUG-0982: the specialty question of the bought city comes up on the guest's screen (the buyer), not on the host's;
   the guest answers Economic (the purchase's specialty) and the city has it in both games.
   SPORE_FIX_OFF=civ.specialty_buyer: the question is on the host's screen: FAIL.
"""
import time

from coop_civ import civ, dist
from play_civ import answer_specialty, frame_city
from twoinst import Checks, keyframe, start


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and npc, f'a player city {cap} and NPC cities {npc}'):
        return ck.result()
    tgt = min(npc, key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    r = guest.call(f'coop.civ buycity {tgt} 500')['out'].strip()
    print('  guest:', r, flush=True)
    h = g = {}
    t0 = time.time()
    while time.time() - t0 < 15:
        h, _ = civ(host)
        g, _ = civ(guest)
        if h.get(tgt, {}).get('player') and g.get(tgt, {}).get('player'):
            break
        time.sleep(1)
    time.sleep(3)
    h, _ = civ(host)
    g, _ = civ(guest)
    print(f"  city {tgt}: host {h.get(tgt)} | guest {g.get(tgt)}", flush=True)
    print('  host', host.call('coop.civ tables')['out'].strip().splitlines()[-1], flush=True)
    print('  guest', guest.call('coop.civ tables')['out'].strip().splitlines()[-1], flush=True)
    ck(h.get(tgt, {}).get('player'), f"the host's game has city {tgt} bought for the player civilization")
    ck(g.get(tgt, {}).get('player'), f"the guest's game shows city {tgt} as the player civilization's")
    ck(h.get(tgt, {}).get('owner') == 1 and g.get(tgt, {}).get('owner') == 1,
       f"the city is the guest's in both games (host {h.get(tgt, {}).get('owner')}, guest {g.get(tgt, {}).get('owner')})")
    # BUG-0982: the buyer is asked for the specialty (the game's own box on the guest's screen), the host's game is
    # neither asked nor paused; the guest takes the purchase's specialty (Economic) and the host's game sets it
    spec0 = hc[tgt].get('spec')
    gq = answer_specialty(guest, 12, prefer='Economic')
    hq = answer_specialty(host, 0)
    print(f"  specialty question: host {hq} | guest {gq} (the city's specialty before: {spec0})", flush=True)
    time.sleep(3)
    h, _ = civ(host)
    g, _ = civ(guest)
    print(f"  specialty now: host {h.get(tgt, {}).get('spec')} | guest {g.get(tgt, {}).get('spec')}", flush=True)
    print('  host', [l for l in host.call('coop.civ tables')['out'].splitlines() if l.startswith('specialty')], flush=True)
    print('  guest', [l for l in guest.call('coop.civ tables')['out'].splitlines() if l.startswith('specialty')], flush=True)
    ck(hq is None, f"the host's game shows no specialty question for the guest's purchase ({hq})")
    if spec0 != 2:  # (a city that already has the purchase's specialty is not asked about)
        ck(gq is not None, f"the guest's game asks the buyer for the city's specialty ({gq})")
        if gq == 'Economic':
            ck(h.get(tgt, {}).get('spec') == 2 and g.get(tgt, {}).get('spec') == 2,
               f"the guest's answer (Economic) is the city's specialty in both games "
               f"(host {h.get(tgt, {}).get('spec')}, guest {g.get(tgt, {}).get('spec')})")
    frame_city({'host': host, 'guest': guest}, h.get(tgt, hc[tgt])['pos'])  # BUG-0981: the city in view
    keyframe('bought', "the bought city in the player's colours on both screens")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
