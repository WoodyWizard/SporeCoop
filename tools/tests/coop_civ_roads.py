#!/usr/bin/env python3
"""Civilization: a road (trade route) the host's game makes between two cities is on the guest too (BUG-0494).

The divergence detector found `Civilization:noun_only_host:inter_city_road`: the host's game had a road
between two cities, the guest's none. A road is the ground object of a trade route (cCity::AddTradeRoute,
type 0, docs/re/civ.md#civ-roads-citizens); the host's AI makes routes, the guest's mirrors never do.

1. Two games on civ_npc.
2. The host's game adds a trade route between its first two cities of the snapshot list
   (`coop.civ roads 0 1`: the game's own AddTradeRoute, as its AI does).
3. 8 s later, the game objects of the road noun (0x2B8A4E7) in each game (`coop.civ nouns 2B8A4E7`): the
   guest has as many roads as the host, and the guest's two cities list the route.
PASS: 3. SPORE_FIX_OFF=civ.roads: the guest has no road (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_civ_roads.py
"""
import re
import time

from twoinst import Checks, start


def roads(con):
    out = []
    for ln in con.call('coop.civ nouns 2B8A4E7')['out'].splitlines():
        m = re.match(r'obj \S+ (-?\d+) (-?\d+) (-?\d+)', ln)
        if m:
            out.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    return out


def routes(con):
    txt = con.call('coop.civ roads')['out']
    return txt, {(int(m.group(1)), int(m.group(2))) for m in re.finditer(r'route (\d+) (-?\d+) type=', txt)}


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    h0, g0 = roads(host), roads(guest)
    print(f'  roads before: host {h0}, guest {g0}', flush=True)
    _, hr = routes(host)
    pair = None
    for a in range(0, 6):
        for b in range(a + 1, 6):
            if (a, b) not in hr:
                pair = (a, b)
                break
        if pair:
            break
    out = host.call(f'coop.civ roads {pair[0]} {pair[1]}')['out']
    print('  host: ' + out.splitlines()[0] if out else '  host: (nothing)', flush=True)
    ck('added: yes' in out, f'the host\'s game made the route {pair[0]}-{pair[1]}')
    time.sleep(8)
    h, g = roads(host), roads(guest)
    print(f'  roads: host {len(h)} {h}', flush=True)
    print(f'  roads: guest {len(g)} {g}', flush=True)
    # (a road object's own position is the planet's centre: counted, not placed)
    ck(len(h) > len(h0), f'the host has a new road ({len(h0)} -> {len(h)})')
    ck(len(g) >= len(h), f'the guest\'s game has every road of the host (host {len(h)}, guest {len(g)})')
    gtxt, gr = routes(guest)
    print('  guest routes:\n    ' + gtxt.strip().replace('\n', '\n    '), flush=True)
    ck(pair in gr, f'the guest\'s cities {pair[0]} and {pair[1]} have the trade route')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
