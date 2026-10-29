#!/usr/bin/env python3
"""Civilization: one frame without the player city anchor changes nothing (BUG-0223).

Civ's local entity is a marker built from the player civ's first city. Before the fix a single frame
in which it was null counted as a world change: the host dropped its vehicle table (new ids, the
guests' games destroyed and rebuilt every vehicle), every city owner and the boosts.

1. The guest buys a vehicle in the host's city: on the host it is the guest's (owner 1).
2. The guest's game has a mirror of every vehicle; the test notes their objects (addresses).
3. The host's local entity is forced null for one frame (`coop.civ nulllocal 1`, the real trigger).
4. On the host the guest's vehicle has the same id and still the guest as owner. On the guest's
   game every vehicle is still the same object (no vehicle was destroyed and rebuilt), and the
   guest's order still drives its vehicle in the host's game (the game's position).
A real load still clears every table: civ_reload (BUG-0059).

--nofix: `coop.fix civ.world_epoch off` (the old trigger): must FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_civ_null_local.py [--nofix]
"""
import argparse
import re
import time

from coop_civ import civ, dist
from twoinst import Checks, start


def objects(con):
    """{vehicle id: game object address} as this machine's `coop.civ` lists them"""
    out = {}
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) (\S+) ', line)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


def tables(con):
    out = con.call('coop.civ tables')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}, out.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix civ.world_epoch off')['out'].strip())
    time.sleep(4)
    hc, hv = civ(host)
    pcities = [k for k, v in hc.items() if v['player']]
    if not ck(pcities, f'player cities on the host: {pcities}'):
        return ck.result()
    city = pcities[0]
    before = set(i for i, v in hv.items() if v['owner'] == 1)
    print(' ', guest.call(f'coop.civ buy {city} 0')['out'].strip())
    time.sleep(4)
    hc, hv = civ(host)
    mine = [i for i, v in hv.items() if v['owner'] == 1 and i not in before] or [i for i, v in hv.items() if v['owner'] == 1]
    if not ck(mine, f'the guest owns a vehicle: {mine}'):
        return ck.result()
    vid = mine[0]
    time.sleep(2)
    g0 = objects(guest)
    t0, txt0 = tables(host)
    print(f'  host vehicles {sorted(hv)}; guest mirrors {g0}')
    print('  host before:', txt0)

    # the frame without the anchor
    print(' ', host.call('coop.civ nulllocal 1')['out'].strip())
    time.sleep(4)
    t1, txt1 = tables(host)
    hc, hv1 = civ(host)
    g1 = objects(guest)
    print('  host after:', txt1)
    print(f'  host vehicles {sorted(hv1)}; guest mirrors {g1}')
    ck(vid in hv1 and hv1[vid]['owner'] == 1, f'the guest\'s vehicle keeps its id and owner on the host ({hv1.get(vid)})')
    same = [i for i in g0 if g1.get(i) == g0[i]]
    ck(g0 and len(same) == len(g0), f'the guest\'s game kept every vehicle object ({len(same)} of {len(g0)})')

    # the guest still drives its vehicle (the host's game moves it)
    p0 = hv1.get(vid, hv[vid])['pos']
    cpos = hc[city]['pos']
    d = dist(p0, cpos) or 1
    tgt = tuple(c + (p - c) / d * (d + 40) for p, c in zip(p0, cpos))
    print(' ', guest.call(f'coop.civ select {vid} 1')['out'].strip())
    print(' ', guest.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))['out'].strip())
    time.sleep(8)
    _, hv2 = civ(host)
    moved = dist(hv2[vid]['pos'], p0) if vid in hv2 else 0
    ck(moved > 5, f'the guest\'s order drives its vehicle in the host\'s game ({moved:.1f} m)')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
