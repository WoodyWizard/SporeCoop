#!/usr/bin/env python3
"""Civilization: wild creatures the host's game parks in its pool while it stands still go on the guest too
(BUG-1089, the Tribe / Civ side of BUG-0339).

Before: in Tribe and Civ the host sent nothing while its simulation stood (the pause menu): host_tick returned at
once, and the Creature stage's frozen pass (npc.frozen_pooled) did not run there. A creature its game took back into
the pool meanwhile (the planet's centre, no herd) kept its proxy on every guest where it had stood, for the whole
pause (the full campaign, lab 20261113-160426: 14 proxies on Bob's ground, 525 m from the host's objects at (0 0 0),
sync-check MISMATCH). In Civ no distance rule drops it either (every guest gets every wild creature).

1. Two games on civ_npc; both cameras on the herd; the guest shows the host's live wild creatures (census by net id).
2. The host opens its pause menu (Esc): its game updates no creature (`frozen_s` of coop.npc).
3. The host's camera moves away from the herd (as the campaign's host did; the relevance pass may park the herd by
   itself, printed), then `coop.npc pool <id>` (the game's own pool call, creature vt 0xC8) on up to 3 of the herd's
   creatures still in the world: they are at the planet's centre in the host's game.
4. For 6 s, while the host still stands: no live proxy of a pooled creature on the guest.
SPORE_FIX_OFF=npc.frozen_pooled_rts: the guest keeps them where they stood until the host's game goes on: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_civ_wild_pooled.py
"""
import math
import re
import time

import census
import uifind
from coop_civ_wild_sync import agree
from play_civ import dist, frame_city
from twoinst import Checks, keyframe, start


def frozen_s(host):
    m = re.search(r'frozen_s=([\d.]+)', host.call('coop.npc')['out'])
    return float(m.group(1)) if m else 0.0


def npcs(con, own):
    """{net id: row} of the live creatures of this game with a net id (host: 'npc', guest: 'proxy')"""
    return {i: r for i, r in census.Census(con.call('coop.census creatures')['out']).by_id('c', (own,)).items() if r['alive']}


def close_pause(con):
    for _ in range(3):
        if not uifind.shown(con, 'Resume Game'):
            return
        uifind.press(con, 'Resume Game')
        time.sleep(1.0)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(5)
    h = npcs(host, 'npc')
    if not ck(len(h) >= 3, f'wild creatures the host\'s game sends ({len(h)})'):
        return ck.result()
    pts = [r['pos'] for r in h.values()]
    c0 = max(pts, key=lambda p: sum(1 for q in pts if dist(p, q) < 30))  # the herd, not a lone epic
    frame_city({'host': host, 'guest': guest}, c0, answer=False)
    time.sleep(4)
    agree(host, guest, 10, 'both cameras on the herd')
    h, g = npcs(host, 'npc'), npcs(guest, 'proxy')
    herd = sorted(i for i, r in h.items() if dist(r['pos'], c0) < 40 and i in g and dist(g[i]['pos'], r['pos']) < 4)
    if not ck(len(herd) >= 2, f'the guest shows the host\'s herd at its place ({herd})'):
        return ck.result()
    # 2. the host's pause menu
    host.call('key esc')
    t0 = time.time()
    while time.time() - t0 < 15 and frozen_s(host) < 1.5:
        time.sleep(0.5)
    if not ck(frozen_s(host) >= 1.5, f'the host\'s game stands still (pause menu, frozen {frozen_s(host):.1f} s)'):
        close_pause(host)
        return ck.result()
    # 3. the host's camera leaves the herd; then its game's pool call on the herd's creatures still in the world
    far = tuple(-v for v in c0)  # the other side of the planet
    frame_city({'host': host}, far, answer=False)
    time.sleep(3)
    h2 = npcs(host, 'npc')
    parked = [i for i in herd if i not in h2 or math.dist(h2[i]['pos'], (0, 0, 0)) < 50]
    print(f'  the host\'s camera left the herd: {len(parked)} of {len(herd)} parked by the game itself {parked}',
          flush=True)
    pooled = list(parked)
    for i in [i for i in herd if i not in parked][:3]:
        out = host.call(f'coop.npc pool {i}')['out'].strip()
        print(f'  host: {out}', flush=True)
        m = re.search(r'now at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
        if m and math.dist(tuple(float(m.group(k)) for k in (1, 2, 3)), (0, 0, 0)) < 50:
            pooled.append(i)
    if not ck(pooled, f'creatures of the host\'s herd in its pool at the planet\'s centre ({pooled})'):
        close_pause(host)
        return ck.result()
    # 4. the guest while the host stands
    time.sleep(2)
    kept, t0 = None, time.time()
    while time.time() - t0 < 6:
        g = npcs(guest, 'proxy')
        kept = [i for i in pooled if i in g]
        print(f'  {time.time() - t0:3.0f} s: host frozen {frozen_s(host):.0f} s, the guest still shows {kept}', flush=True)
        time.sleep(2)
    keyframe('pooled', 'the guest\'s view of the herd while the host is paused')
    ck(frozen_s(host) > 6, 'the host\'s game stood still for the whole check')
    ck(kept == [], f'the guest shows none of the creatures the host\'s game parked in its pool ({kept})')
    close_pause(host)
    time.sleep(2)
    ck(frozen_s(host) == 0, 'the host\'s game goes on after the pause menu')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
