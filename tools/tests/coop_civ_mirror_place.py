#!/usr/bin/env python3
"""Civilization: the guest's game does not throw a mirrored vehicle off its place (BUG-0186).

In every play_civ.py seed 9 / 11 session one NPC vehicle of the civ_npc fixture (net id 2, nation
000021CB) claims the spice node near (-336, 132, 354). The host's game keeps it there; the guest's
game finds the ground under the mirror invalid for a land vehicle (Vehicle_KeepOnValidGround
0xCA7430) and puts it on the nearest valid spot, 13 m away, every frame, and guest_tick puts it
back: the guest showed it 13-15 m off at times.

1. Two games on civ_npc; the host's NPC vehicles go about their business (no command).
2. The guest's `coop.civ pushprobe` logs who moves a mirror (tests).
3. For 40 s, every 0.5 s: every mirrored NPC vehicle's position on the guest's game against the
   host's (the guest's `coop.civ` line, read right after the host's).
PASS: no mirror farther than 4 m from the host's vehicle (a vehicle driving at 12 u/s between the
two reads is ~1 m) and the vehicle at the node was seen. --nofix (`coop.fix civ.mirror_place off`):
must FAIL (13 m).

  SPORE_LANE=7 python3 tools/tests/coop_civ_mirror_place.py [--nofix]
"""
import argparse
import math
import time

from play_civ import civ_state
from twoinst import Checks, start

NODE = (-336.0, 132.0, 354.0)  # the spice node of the civ_npc fixture the vehicle claims


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--seconds', type=float, default=40)
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix civ.mirror_place off')['out'].strip(), flush=True)
    time.sleep(3)
    print('  guest:', guest.call('coop.civ pushprobe')['out'].strip(), flush=True)
    worst, worst_id, n, near_node = 0.0, None, 0, 0
    t0 = time.time()
    while time.time() - t0 < a.seconds:
        _, hv = civ_state(host)[:2]
        _, gv = civ_state(guest)[:2]
        for i, v in hv.items():
            if v['owner'] != 254 or i not in gv:
                continue
            d = math.dist(v['pos'], gv[i]['pos'])
            near_node += math.dist(v['pos'], NODE) < 8
            if d > worst:
                worst, worst_id = d, i
        n += 1
        time.sleep(0.5)
    st = civ_state(guest)[2]
    print(f'  {n} samples: largest distance {worst:.1f} m (vehicle #{worst_id}), samples at the node {near_node}', flush=True)
    print('  guest:', st[st.find('mirror_place'):], flush=True)
    ck(n >= 30, f'samples of both games ({n})')
    ck(near_node > 0, f'an NPC vehicle of the host stood at the spice node ({near_node} samples)')
    ck(worst < 4.0, f'the guest shows every NPC vehicle where the host has it (largest distance {worst:.1f} m, #{worst_id})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
