#!/usr/bin/env python3
"""Civilization: an order of the guest's own game does not move a mirrored vehicle (BUG-0186).

In play_civ.py a guest's mirror of an NPC vehicle had an order of its own (`order=8` on the guest,
none of the host's) and the guest's game moved it 13 m from where the host had it. The guest's
SetOrder hook only drops orders for vehicles already bound as mirrors and given through SetOrder;
an order given before the binding, or by a vehicle behaviour, stayed.

1. Two games on civ_npc. The guest picks mirrors of the NPC nations' vehicles (owner 254).
2. `coop.civ localorder <id> <type>` makes the guest's own game give each of them an order the way
   its AI does (the hook is bypassed): 8 (claim the nearest spice node) and 6 (move 60 m away).
3. For 20 s the guest's game is sampled: its order list of those vehicles (`order=` of the
   guest's coop.civ: what the guest's game will act on) and the distance between the guest's
   vehicle and the host's.
PASS: the guest's game holds no order for them after the first second, and the guest shows them
within 3 m of the host. --nofix (`coop.fix civ.mirror_orders off`): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_mirror_order.py [--nofix]
"""
import argparse
import math
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
            print(' ', c.call('coop.fix civ.mirror_orders off')['out'].strip(), flush=True)
    time.sleep(5)
    print('  guest:', guest.call('coop.civ pushprobe')['out'].strip(), flush=True)  # logs who moves a mirror
    _, hv = civ_state(host)[:2]
    _, gv = civ_state(guest)[:2]
    npc = sorted(i for i, v in gv.items() if v['owner'] == 254 and i in hv)
    ck(len(npc) >= 2, f'the guest mirrors NPC vehicles ({npc})')
    if len(npc) < 2:
        return ck.result()
    picked = npc[:2]
    p = gv[picked[1]]['pos']
    out = [guest.call(f'coop.civ localorder {picked[0]} 8')['out'].strip(),
           guest.call(f'coop.civ localorder {picked[1]} 6 {p[0] + 40:.1f} {p[1]:.1f} {p[2] + 40:.1f}')['out'].strip()]
    print('  guest orders:', out, flush=True)
    ck(all('given here' in o for o in out), 'the guest\'s own game gave both mirrors an order')
    time.sleep(1)
    held, worst, n = {i: 0 for i in picked}, {i: 0.0 for i in picked}, 0
    worst_line = {}
    t0 = time.time()
    while time.time() - t0 < 20:
        _, hv = civ_state(host)[:2]
        _, gv = civ_state(guest)[:2]
        for i in picked:
            if i in hv and i in gv:
                held[i] += gv[i]['order'] >= 0
                d = math.dist(hv[i]['pos'], gv[i]['pos'])
                if d > worst[i]:
                    worst[i] = d
                    worst_line[i] = [ln.strip() for c in (host, guest) for ln in c.call('coop.civ')['out'].splitlines()
                                     if ln.strip().startswith(f'veh #{i} ')]
        n += 1
        time.sleep(0.5)
    for i, lines in worst_line.items():
        for ln in lines:
            print(f'    worst #{i}: {ln[:330]}', flush=True)
    st = civ_state(guest)[2]
    print(f'  {n} samples: orders held by the guest\'s game {held}, largest distance to the host {worst}', flush=True)
    print('  guest:', st[st.find('mirror_orders'):], flush=True)
    ck(all(v <= 1 for v in held.values()), f'the guest\'s game holds no order of its own for the mirrors ({held})')
    ck(all(v < 3.0 for v in worst.values()),
       f'the guest shows the vehicles where the host has them (largest distance {max(worst.values()):.1f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
