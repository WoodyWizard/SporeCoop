#!/usr/bin/env python3
"""Tribe: the guest's player tribe has the host's level and population cap (BUG-0645).

Player report 2026-11-06 (0.7.8): the guest "can't spawn more than 6 members" and its village shows 12/6. The
population cap is the player tribe's level's (cTribe +0x38: 6, 9, 12, 15, 30, 45, 60), set by the hut upgrade
0xCDA4F0 when the goal progress passes a threshold, which also copies the cap into the Tribe HUD. A guest's game
gets the host's members and goal progress but never runs the upgrade: level 0, cap 6.

1. Two games on the tribe fixture.
2. The host's tribe goes to level 2 (`coop.tribe level 2`: the two stores of the hut upgrade) and to 10 members.
3. For 15 s: the guest's player tribe (the game's own cap function and the HUD's cap).
PASS: the guest's tribe has the host's level, its cap and HUD cap are the host's (12), with the host's members.
--nofix (`coop.fix wsync.tribe_level off`): must FAIL (cap 6 with 10 members).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_level.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def player_line(con):
    for ln in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe \d+ \(player\) .*: (\d+) members .*level=(-?\d+) cap=(-?\d+) hud_cap=(-?\d+)', ln)
        if m:
            return tuple(int(x) for x in m.groups())
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix wsync.tribe_level off')['out'].strip(), flush=True)
    print('  host:', player_line(host), '| guest:', player_line(guest), flush=True)
    print('  host:', host.call('coop.tribe level 2')['out'].strip(), flush=True)
    h = player_line(host)
    n = max(0, 10 - (h[0] if h else 0))
    print('  host:', host.call(f'coop.tribe spawn {n}')['out'].strip(), flush=True)
    t0, g, h = time.time(), None, None
    while time.time() - t0 < 15:
        h, g = player_line(host), player_line(guest)
        if h and g and g[1:] == h[1:] and g[0] == h[0]:
            break
        time.sleep(1)
    print(f'  host (members, level, cap, hud cap) {h} | guest {g}', flush=True)
    ck(h is not None and h[1] == 2 and h[2] == 12, f'the host\'s tribe is at level 2, cap 12 ({h})')
    ck(g is not None and h is not None and g[0] == h[0], f'the guest shows the host\'s members ({g and g[0]} / {h and h[0]})')
    ck(g is not None and g[1] == 2, f'the guest\'s player tribe has the host\'s level ({g and g[1]})')
    ck(g is not None and g[2] == 12, f'the guest\'s game gives its player tribe the host\'s cap ({g and g[2]}, host 12)')
    ck(g is not None and (g[3] == 12 or g[3] == -1), f'the guest\'s HUD cap is the host\'s ({g and g[3]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
