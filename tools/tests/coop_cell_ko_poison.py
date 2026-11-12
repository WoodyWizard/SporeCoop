#!/usr/bin/env python3
"""Cell: a knocked-out cell makes no poison clouds (BUG-0799).

The knockout holds a cell at hp 1 (alive) for 10 s. The game's poison step (0xE68630 -> Cell_PoisonCloud
0xE76660) asks only for a live cell, so a knocked-out cell with a poison part went on making clouds, which
hurt and killed creatures and counted as that player's kills. The host makes every cloud of the pool:
its own avatar's (the game's step) and a guest's (puppet_poison, BUG-0716).

The cells get the poison part the way coop_cell_parts_attack.py gives it (`coop.pool borrow poison graft`,
for the host's own avatar and for the guest's puppet in the host's game). Counted in the HOST's game: the
poison cells (cell type 5, `coop.pool see`) it has on its screen, by their keys; a new key is a new cloud.
1. --who host (default): the guest is 30 m away; control: the host's avatar makes clouds (new keys within
   15 s); the host is knocked out (`coop.pool selfhit 50`): no new cloud key in its game for the 8 s of
   the knockout (after 1 s, the clouds made before stay their 3-7 s).
2. --who guest: the host stays 8 m from the guest (its game draws the guest's cell, the poison step needs
   it; too far to revive); control: the host's game makes the guest's clouds (new keys near the guest's
   puppet); the guest is knocked out: no new cloud key near its puppet in the host's game for 8 s.
SPORE_FIX_OFF=cell.ko_no_poison must FAIL (new clouds while knocked out).

  SPORE_LANE=2 python3 tools/tests/coop_cell_ko_poison.py --who host|guest
"""
import argparse
import re
import time

from coop_cell_ko_food import wait_downed
from coop_cell_puppet_scale import playing, wait
from twoinst import Checks, start


def clouds(con):
    """keys of the poison cells this game has on its screen"""
    out = con.call('coop.pool see 400')['out']
    return {m.group(1) for m in re.finditer(r'^see (c\d+) poison ', out, re.M)}


def new_clouds(con, seen, secs, step=0.3):
    """keys that were not in `seen`, over secs (seen grows)"""
    new, t0 = set(), time.time()
    while time.time() - t0 < secs:
        k = clouds(con)
        new |= k - seen
        seen |= k
        time.sleep(step)
    return new


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--who', choices=('host', 'guest'), default='host')
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    if a.who == 'host':
        guest.call('coop.pool tp 30 0')
        got = host.call('coop.pool borrow poison graft')['out'].strip()
    else:
        guest.call('coop.pool tp 8 0')
        got = host.call(f'coop.pool borrow poison graft p{gid}')['out'].strip()
    print('  host: ' + got, flush=True)
    ck(got.startswith('borrowed'), f'the {a.who}\'s cell has the poison part in the host\'s game')
    if not got.startswith('borrowed'):
        return ck.result()
    seen = clouds(host)
    made = new_clouds(host, seen, 15)
    print(f'  control: {len(made)} new clouds in 15 s', flush=True)
    ck(len(made) >= 2, f'control: the host\'s game makes the {a.who}\'s poison clouds ({len(made)} in 15 s)')
    who = host if a.who == 'host' else guest
    print(f'  {a.who}: ' + who.call('coop.pool selfhit 50')['out'].strip(), flush=True)
    down = wait_downed(who, 1)
    ck(down, f'the {a.who} is knocked out')
    t0 = time.time()
    new_clouds(host, seen, 1.0)  # (made in the frame of the knockout)
    during = new_clouds(host, seen, 8.0)
    still = wait_downed(who, 1, 0.5)
    print(f'  while knocked out ({time.time() - t0:.1f} s): {len(during)} new clouds {sorted(during)[:8]}, still out: {still}',
          flush=True)
    print('  host: ' + ' '.join(x for x in host.call('coop.pool')['out'].splitlines() if x.startswith('cell_c1')), flush=True)
    ck(down and still, f'the {a.who} stayed knocked out while it was watched')
    ck(not during, f'a knocked-out {a.who} makes no poison clouds in the host\'s game ({len(during)} new in 8 s)')
    host.call('coop.pool borrow off' if a.who == 'host' else f'coop.pool borrow off x p{gid}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
