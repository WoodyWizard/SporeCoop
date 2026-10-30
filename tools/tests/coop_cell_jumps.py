#!/usr/bin/env python3
"""Cell: a player whose cell jumps far again and again does not fill its pool with background cells
(BUG-0314, found hunting BUG-0307).

In lab 20261030-104149 campaign#2 a guest's own cell went NaN and was put back again and again: its
position jumped 100-200 m at a time, its mirrors swung 900 -> 43 -> 900 and its coverage of the
host's cells fell to 1%. Reproduced with jumps alone (coop_cell_4p.py --jumps, 4 players and 2):
every jump left the background decoration cells (query G+0x4100) of the place behind in the pool;
the pool reached the guest's mirror cap (1500 live cells, BUG-0295) and the guest created no mirror
any more (0 of the host's cells on its screen).

1. The host's food goes to --food (a level with bigger tiles).
2. --who (guest or host) jumps --jumps times to a random place 150-250 m from the other player,
   every --gap s; `coop.pool census` of the jumper every 5 jumps.
3. 15 s after the last jump: the jumper's pool holds at most --max background cells, and (a guest)
   it mirrors the host's cells around it again (`see`: cells of the host's world on its screen).
SPORE_FIX_OFF=cell.bg_cull must FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_cell_jumps.py [--who guest] [--jumps 40] [--gap 3] [--food 300]
"""
import argparse
import math
import random
import re
import time

from twoinst import Checks, start


def census(con):
    out = con.call('coop.pool census')['out'].strip()
    vals = {k: int(v) for k, v in re.findall(r'\b([a-z_]+)=(\d+)', out)}
    return out, vals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--who', default='guest', choices=('guest', 'host'))
    ap.add_argument('--jumps', type=int, default=40)
    ap.add_argument('--gap', type=float, default=3)
    ap.add_argument('--food', type=int, default=300)
    ap.add_argument('--max', type=int, default=600)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    if a.food:
        print('  host: food', host.call(f'coop.pool food {a.food}')['out'].strip()[:80], flush=True)
        time.sleep(8)
    j = guest if a.who == 'guest' else host
    out, v0 = census(j)
    print('  start:', out, flush=True)
    rng = random.Random(5)
    for k in range(a.jumps):
        ang, r = rng.uniform(0, 2 * math.pi), rng.uniform(150, 250)
        j.call(f'coop.pool tp {r * math.cos(ang):.0f} {r * math.sin(ang):.0f}')
        time.sleep(a.gap)
        if k % 5 == 4:
            print(f'  jump {k + 1}:', census(j)[0], flush=True)
    j.call('coop.pool tp 20 0')  # next to the other player
    time.sleep(15)
    out, v = census(j)
    print('  after:', out, flush=True)
    bg = v.get('background', -1)
    ck(0 <= bg <= a.max, f'3. the {a.who}\'s pool holds at most {a.max} background cells after the jumps ({bg}, '
       f'{v0.get("background", "?")} before; live {v.get("live")})')
    if a.who == 'guest':
        seen = [line for line in guest.call('coop.pool see 300')['out'].splitlines() if line.startswith('see ')]
        ck(len(seen) > 0, f'3. the guest sees the host\'s cells on its screen again ({len(seen)}, mirrors {v.get("mirrors")})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
