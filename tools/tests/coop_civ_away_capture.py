#!/usr/bin/env python3
"""Civilization: a city a guest captures while the host is away is captured in both games when the host is back
(BUG-0878).

While the host is away (a menu, an editor, a load) the guest's game plays on by itself (civ.away): its vehicles
fight and the city's capture meter fills in its game, which shows the capture won, but Capture is refused on a
guest and nothing went to the host: the city stayed the other nation's in both games.

1. Two games on civ_npc. The host goes to the main menu (`coop.menu`); once the guest's game knows (host_away=1),
   the guest's game fills the nearest NPC city's capture meter for the player civilization (`coop.civ siege <city> 1
   150 pc`: capture damage through the game's AddCaptureDamage; its own CheckMilitaryCapture captures at 100).
2. The host loads its world again. Within 40 s of the guest seeing it back, on both games: the city's political id
   is the player civilization's and its owner is the guest (1).
   SPORE_FIX_OFF=civ.away_capture: the city is the other nation's in both games: FAIL.
"""
import time

from coop_civ import civ, dist
from play_civ import frame_city
from twoinst import Checks, keyframe, lane_state, start


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
    tpos = hc[tgt]['pos']
    print(' ', host.call('coop.menu')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 90 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    if not ck('host_away=1' in guest.call('coop.status')['out'], 'the guest knows the host is away'):
        return ck.result()
    r = guest.call(f'coop.civ siege {tgt} 1 150 pc')['out'].strip()
    print('  guest:', r, flush=True)
    time.sleep(2)
    print('  guest', guest.call('coop.civ tables')['out'].strip().splitlines()[-1], flush=True)
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Tavael').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 150 and 'host_away=0' not in guest.call('coop.status')['out']:
        time.sleep(2)
    ck('host_away=0' in guest.call('coop.status')['out'], 'the host is back')
    h = g = {}
    hi = gi = None
    t0 = time.time()
    while time.time() - t0 < 40:
        h, _ = civ(host)
        g, _ = civ(guest)
        # the city by its place (the host's indices are made anew by the load)
        hi = min(h, key=lambda i: dist(h[i]['pos'], tpos)) if h else None
        gi = min(g, key=lambda i: dist(g[i]['pos'], tpos)) if g else None
        if hi is not None and gi is not None and h[hi]['player'] and g[gi]['player']:
            break
        time.sleep(2)
    time.sleep(3)
    h, _ = civ(host)
    g, _ = civ(guest)
    hc_, gc_ = h.get(hi, {}), g.get(gi, {})
    print(f'  the city at {tpos}: host #{hi} {hc_} | guest #{gi} {gc_}', flush=True)
    print('  host', host.call('coop.civ tables')['out'].strip().splitlines()[-1], flush=True)
    ck(hc_.get('player'), "the host's game has the city captured for the player civilization")
    ck(gc_.get('player'), "the guest's game shows the city as the player civilization's")
    ck(hc_.get('owner') == 1 and gc_.get('owner') == 1,
       f"the city is the guest's in both games (host {hc_.get('owner')}, guest {gc_.get('owner')})")
    frame_city({'host': host, 'guest': guest}, tpos)  # BUG-0981: the city in view, not the capital
    keyframe('away_captured', "the city the guest took, in the player's colours on both screens")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
