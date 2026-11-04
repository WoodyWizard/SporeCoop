#!/usr/bin/env python3
"""Tribe: a tool a guest buys and places is placed in the host's game too, and its members use it (BUG-0524).

The palette's placement made the tool in the buyer's game only: the host had no rack there, a guest's GrabTool
order on it was rejected on the host ("no such target here"). With wobj.tribe_tool_buy the guest's new tool is a
request to the host, which places the same (the old one of the type goes, as the palette does).

1. Tribe fixture. The guest buys a weapon rack (`coop.tribe buy 1 9 120`: the palette's placement in its game,
   CreateTool, dragged by the cursor (spatial +0x72) for 4 s, then `buy_end`: placed, the purchased bit; test
   setup: the fixture's palette is locked).
2. While it is dragged the host's game has no new rack (a tool is placed when the drag ends).
3. Within 30 s the host's game has a rack of that type within 2 m of the guest's (`coop.tribe tools` on the host:
   the game's own tool list), and the guest's own rack is still there (one of the type).
4. A guest's member is ordered to the rack (`coop.tribe grab`, the order a click gives, run on the host): within
   90 s it holds the weapon in the host's game and on the guest's screen.

SPORE_FIX_OFF=wobj.tribe_tool_buy: must FAIL (no rack in the host's game).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_tool_buy.py [--type 1]
"""
import argparse
import re
import time

from coop_tribe_tools import dist, members, tools
from twoinst import Checks, shot, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--type', type=int, default=1)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    out = guest.call(f'coop.tribe buy {a.type} 9 120')['out'].strip()
    print('  guest:', out)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    ck(bool(m), f'the guest is placing a tool of type {a.type}')
    if not m:
        ck.faults(host=host, guest=guest)
        return ck.result()
    at = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(4)
    hd, _ = tools(host)
    ck(not any(t == a.type and dist(p, at) < 3 for _, t, p in hd), 'while the guest drags it the host has no rack there')
    print('  guest:', guest.call('coop.tribe buy_end')['out'].strip())
    rack = None
    t0 = time.time()
    while time.time() - t0 < 30 and rack is None:
        time.sleep(2)
        ht, _ = tools(host)
        near = [(i, t, p) for i, t, p in ht if t == a.type and dist(p, at) < 2]
        rack = near[0] if near else None
    print('  host', host.call('coop.wobj')['out'].strip().splitlines()[-1])
    print('  guest', guest.call('coop.wobj')['out'].strip().splitlines()[-1])
    ck(rack is not None, f'the host\'s game has the guest\'s rack (type {a.type} within 2 m of {at}): {rack[2] if rack else "none"}')
    time.sleep(4)
    gt, _ = tools(guest)
    mine = [(i, t, p) for i, t, p in gt if t == a.type]
    ck(len(mine) == 1 and dist(mine[0][2], at) < 2, f'the guest\'s game keeps its one rack of the type there ({[p for _, _, p in mine]})')
    if rack is None or len(mine) != 1:
        print('  host tools:', tools(host)[0])
        ck.faults(host=host, guest=guest)
        return ck.result()
    own = sorted(i for i, o in members(guest).items() if o != 0)
    ck(bool(own), f'the guest has members of its own ({len(own)})')
    if not own:
        ck.faults(host=host, guest=guest)
        return ck.result()
    _, hh = tools(host)
    pick = next((i for i in own if hh.get(i, 0) not in (a.type, 11)), own[0])
    print('  guest:', guest.call(f'coop.tribe grab {pick} {mine[0][0]}')['out'].strip())
    held_h = held_g = None
    t0 = time.time()
    again = False
    while time.time() - t0 < 90:
        time.sleep(3)
        _, hh = tools(host)
        _, gh = tools(guest)
        held_h, held_g = hh.get(pick), gh.get(pick)
        if held_h == a.type and held_g == a.type:
            break
        if not again and time.time() - t0 > 40 and held_h != a.type:
            again = True  # as coop_tribe_tools.py: a player clicks the rack again
            print('  guest (again):', guest.call(f'coop.tribe grab {pick} {mine[0][0]}')['out'].strip())
    m = re.search(r'tribe last order: (.*)', host.call('coop.status')['out'])
    print('  host order:', m.group(1) if m else '')
    ck(held_h == a.type, f'the guest\'s member #{pick} took the weapon in the host\'s game (tool {held_h})')
    ck(held_g == a.type, f'the guest sees member #{pick} hold the weapon (tool {held_g}, {time.time() - t0:.0f} s)')
    print('  shots:', shot(host, 'tribe_tool_buy_host.png'), shot(guest, 'tribe_tool_buy_guest.png'))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
