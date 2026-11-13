#!/usr/bin/env python3
"""BUG-0975: Cell: a guest at a higher level that joins a host at a lower one dies right after the join.

At the join the guest takes the host's food total in one AddFoodPoints (327 -> 0 in the lab: level 4 to 1).
The game's world scale G+0x514C stays at the old level's for ~3 s while PopulateTiles builds the new
level's active set from it: the lowest levels' tiles are tiny, the game writes thousands of entries into
its stack buffer of 511 and the process dies (0xC0000005 at 0x00E5FA50, no crash report).
docs/re/cell.md#cell-level-fall.

--case join (the players' path): two games in the Cell fixture; the guest leaves, plays on alone to food
  330 (level 4: `coop.pool food 330`, its own game) and joins the host again (food 0). Checked on the guest:
  the game still answers, its food is the host's (the game's S+0x1C) and its level the host's, its own cell
  is there; no fault on either game.
--case solo: no session at all: the guest leaves, goes to food 330 and back to 0 in one call (the game's
  AddFoodPoints, as the debug keys call it with -50). Shows that the game's own code dies on such a fall.

SPORE_FIX_OFF=cell.tile_cap must FAIL (the guest's game is gone).

  SPORE_LANE=6 python3 tools/tests/coop_cell_level_drop.py [--case join|solo]
"""
import argparse
import re
import time

import lane
from twoinst import Checks, keyframe, start, wait_guest_in


def pool(con):
    try:
        out = con.call('coop.pool')['out']
    except OSError:
        return None
    m = re.search(r'\bfood=(-?\d+)', out)
    t = re.search(r'tilecap_skips=(\d+) tilecap_max=(\d+)', out)
    return {'food': int(m.group(1)) if m else None, 'skips': int(t.group(1)) if t else None,
            'max': int(t.group(2)) if t else None}


def answers(con):
    try:
        return con.call('ping')['ok']
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('join', 'solo'), default='join')
    ap.add_argument('--food', type=int, default=330)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    f0 = pool(host)['food']
    print(f'  host food {f0}', flush=True)
    guest.call('coop.leave')
    time.sleep(2)
    guest.call(f'coop.pool food {a.food - (pool(guest)["food"] or 0)}')  # alone: its own game levels up
    time.sleep(12)  # the level changes and the world scale settles
    g1 = pool(guest)
    print(f'  guest alone: food {g1["food"]}', flush=True)
    ck(g1['food'] is not None and g1['food'] >= a.food - 5, f'the guest played on alone to food {g1["food"]} (wanted {a.food})')
    keyframe('high', 'the guest alone at the higher level: its cell bigger, the cells round it of that level')
    if a.case == 'join':
        guest.call(lane.JOIN)
        try:
            joined = wait_guest_in(host, guest, 120)
        except OSError:  # the guest's game died while joining (the bug)
            joined = False
        ck(joined or answers(guest), 'the guest joined the host again')
    else:
        guest.call(f'coop.pool food {-g1["food"]}')
    alive_all = True
    for _ in range(20):  # the fall and the ~3 s of the world scale easing to the new level
        time.sleep(1)
        alive_all &= answers(guest)
        if not alive_all:
            break
    ck(alive_all, 'the guest\'s game still answers after the fall of its level')
    if not alive_all:
        ck.faults(host=host)
        return ck.result()
    g2, h2 = pool(guest), pool(host)
    want = h2['food'] if a.case == 'join' else 0
    ck(g2['food'] is not None and abs(g2['food'] - want) <= 30,
       f'the guest\'s food is {g2["food"]} ({"the host" if a.case == "join" else "wanted"}: {want})')
    print(f'  guest tile passes skipped for the buffer: {g2["skips"]} (largest active set {g2["max"]})', flush=True)
    try:
        av = guest.call('coop.avatar')['ok']
    except OSError:
        av = False
    ck(av, 'the guest\'s own cell is there')
    keyframe('low', 'the guest after the fall: its cell at the low level\'s size, cells of that level round it, '
                    'no empty or garbage screen')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
