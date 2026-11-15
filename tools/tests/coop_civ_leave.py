#!/usr/bin/env python3
"""Civilization: the vehicles of a guest who left (BUG-0031).

1. The guest buys a vehicle in a player city (owner = the guest on the host).
2. The guest leaves the session (`coop.leave`); the host plays on.
3. On the host the vehicle has a present owner (the host), the host selects it in the game (the
   vehicle's selected flag stays set: before the fix the selection was dropped at once) and drives
   it: the game's position changes.
4. A capture next to it would name the owner of the nearest vehicle: no vehicle has an absent owner.

civ.orphans off failed (retired in 0.7.15, BUG-1138: FAIL run 20261030-011312:party_civ_own); the test runs
PASS-only.

  SPORE_LANE=7 python3 tools/tests/coop_civ_leave.py
"""
import argparse
import time

from coop_civ import civ, dist
from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    time.sleep(4)
    hc, hv = civ(host)
    pcities = [k for k, v in hc.items() if v['player']]
    if not ck(pcities, f'player cities on the host: {pcities}'):
        return ck.result()
    city = pcities[0]
    before = set(i for i, v in hv.items() if v['owner'] == 1)
    print(' ', guest.call(f'coop.civ buy {city} 0')['out'].strip())
    time.sleep(3)
    hc, hv = civ(host)
    mine = [i for i, v in hv.items() if v['owner'] == 1 and i not in before] or [i for i, v in hv.items() if v['owner'] == 1]
    if not ck(mine, f'the guest owns a vehicle: {mine}'):
        return ck.result()
    vid = mine[0]
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    hc, hv = civ(host)
    owners = sorted(set(v['owner'] for v in hv.values()))
    print(f'  host after the leave: vehicle #{vid} owner {hv.get(vid, {}).get("owner")}, owners {owners}')
    ck(vid in hv and hv[vid]['owner'] == 0, 'the leaver\'s vehicle belongs to the host now')
    ck(all(o in (0, 254) for o in owners), 'no vehicle has an absent owner (a capture could name it)')
    r = host.call(f'coop.civ select {vid} 1')['out'].strip()
    time.sleep(1.5)  # the host drops foreign vehicles from its selection every 0.1 s
    hc, hv = civ(host)
    sel = hv.get(vid, {}).get('sel')
    print(f'  select: {r}; 1.5 s later sel={sel}')
    ck(sel == 1, 'the host selects it in the game and the selection stays')
    p0 = hv[vid]['pos']
    cpos = hc[city]['pos']
    d = dist(p0, cpos) or 1
    tgt = tuple(c + (p - c) / d * (d + 40) for p, c in zip(p0, cpos))
    print(' ', host.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))['out'].strip())
    time.sleep(8)
    _, hv = civ(host)
    moved = dist(hv[vid]['pos'], p0) if vid in hv else 0
    ck(moved > 5, f'the host drives it ({moved:.1f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
