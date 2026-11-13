#!/usr/bin/env python3
"""Civilization: while the host is away the guest's game keeps its mirror vehicles on valid ground (BUG-1056).

A guest's game skips Vehicle_KeepOnValidGround for every mirror vehicle (BUG-0186: the host's vehicle stands there,
the host's states put the mirror back). While the host is away (main menu, editor) the guest's own game drives its
mirrors (BUG-0151, civ.away) and no host state comes: a mirror on ground its locomotion does not allow stayed there.

The spice node of the civ_npc fixture near (-336, 132, 354) is ground the guest's game finds invalid for a land
vehicle (BUG-0186: it moved the mirror there 13 m away every frame).
1. Two games on civ_npc; the host goes to the main menu (`coop.menu`), the guest knows it is away.
2. The guest puts a land mirror vehicle (an NPC's) onto the node (`coop.civ vput`: the vehicle's SetPosition, as a
   drive of its own game can leave it there), three times.
3. 0.3 s after each put, the guest's game: the vehicle's own position. Its game's ground check moves it off the node
   (several metres in one frame, more than its drive makes in 0.3 s): at least one of the three puts ends over 6 m away.
SPORE_FIX_OFF=civ.away_ground: the check stays skipped, the vehicle stays on the node (FAIL).

  SPORE_LANE=7 python3 tools/tests/coop_civ_away_ground.py
"""
import math
import time

from play_civ import civ_state
from twoinst import Checks, start

NODE = (-336.0, 132.0, 354.0)  # the spice node of the civ_npc fixture (coop_civ_mirror_place.py)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    _, gv, _ = civ_state(guest)
    land = sorted(i for i, v in gv.items() if v['owner'] == 254 and v['loco'] == gv.get(2, v)['loco'])
    print(f'  guest mirrors: {len(gv)}, NPC land vehicles {land[:8]} (loco {gv.get(2, {}).get("loco")})', flush=True)
    if not ck(land, 'the guest mirrors NPC land vehicles'):
        return ck.result()
    print(' ', host.call('coop.menu')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 90 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    if not ck('host_away=1' in guest.call('coop.status')['out'], 'the guest knows the host is away'):
        return ck.result()
    time.sleep(3)
    _, gv, st = civ_state(guest)
    vid = next((i for i in land if i != 2 and i in gv), land[0])
    best = 0.0
    for k in range(3):
        out = guest.call('coop.civ vput %d %.1f %.1f %.1f' % ((vid,) + NODE))['out'].strip()
        time.sleep(0.3)
        _, gv, st = civ_state(guest)
        d = math.dist(gv[vid]['pos'], NODE) if vid in gv else -1
        best = max(best, d)
        print(f'  put {k + 1}: {out} | 0.3 s later {d:.1f} m from the node', flush=True)
        time.sleep(1.5)
    print('  guest:', st[st.find('local='):], flush=True)
    ck(best > 6.0, f"the guest's game moves its mirror off ground its locomotion does not allow (farthest {best:.1f} m)")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
