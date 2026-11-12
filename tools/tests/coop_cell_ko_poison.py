#!/usr/bin/env python3
"""Cell: a knocked-out cell makes no poison clouds (BUG-0799).

The knockout holds a cell at hp 1 (alive) for 10 s. The game's poison step (0xE68630 -> Cell_PoisonCloud
0xE76660) asks only for a live cell, so a knocked-out cell with a poison part went on making clouds, which
hurt and killed creatures and counted as that player's kills. The host makes every cloud of the pool:
its own avatar's (the game's step) and a guest's (puppet_poison, BUG-0716).

The cells get the poison part the way coop_cell_parts_attack.py gives it (`coop.pool borrow poison graft`,
for the host's own avatar and for the guest's puppet in the host's game). Counted in the HOST's game: the
poison cells (cell type 5, `coop.pool see`) it has on its screen, by their keys; a new key is a new cloud.
1. --who host (default): the guest is 60 m away and invulnerable; control: the host's avatar makes clouds (new keys within
   15 s); the host is knocked out (`coop.pool selfhit 50`): no new cloud key in its game for the 8 s of
   the knockout (after 1 s, the clouds made before stay their 3-7 s). Only clouds within 3 m of the knocked-out
   cell count (the other cell shares the grafted model and makes its own).
2. --who guest: the guest is invulnerable until its knockout (the host's clouds), the host stays 6 m from it (its game draws the guest's cell, the poison step needs
   it; too far to revive); control: the host's game makes the guest's clouds (new keys near the guest's
   puppet); the guest is knocked out: no new cloud key near its puppet in the host's game for 8 s.
SPORE_FIX_OFF=cell.ko_no_poison must FAIL (new clouds while knocked out).

  SPORE_LANE=2 python3 tools/tests/coop_cell_ko_poison.py --who host|guest
"""
import argparse
import math
import re
import time

from coop_cell_ko_food import wait_downed
from coop_cell_puppet_scale import playing, wait
from twoinst import Checks, start

NEAR = 3.0  # m: a cloud starts at a part of its maker
TRACE, T0 = [], time.time()  # (diagnostics: where the counted clouds were)


def clouds(con):
    """keys of the poison cells this game has on its screen"""
    out = con.call('coop.pool see 400')['out']
    return {m.group(1) for m in re.finditer(r'^see (c\d+) poison ', out, re.M)}


def pos(con, key):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call(f'coop.pool cell {key}')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def maker_pos(con, who):
    """where the host's game has the cell of `who` ('host': its own, else the guest's puppet p<id>)"""
    out = con.call('coop.pool scalepos')['out']
    m = re.search(r'me=\((-?[\d.]+) (-?[\d.]+)\)' if who == 'host' else rf'\bp{who}=\((-?[\d.]+) (-?[\d.]+)\)', out)
    return (float(m.group(1)), float(m.group(2))) if m else None


def new_clouds(con, seen, secs, who, step=0.3):
    """keys of new poison cells within NEAR m of `who`'s cell (the other player's cell shares the model and makes its
    own clouds), over secs (seen grows)"""
    new, t0 = set(), time.time()
    while time.time() - t0 < secs:
        k = clouds(con)
        at = maker_pos(con, who)
        for c in k - seen:
            p = pos(con, c)
            if p and at and math.dist(p, at) < NEAR:
                new.add(c)
                if len(TRACE) < 12:
                    TRACE.append((round(time.time() - T0, 1), c, p, at, maker_pos(con, 'host'),
                                  con.call(f'coop.pool cell {c}')['out'].strip()[:90]))
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
        # far, and safe from the host's clouds (they knocked it out in a run: then the host has no helper and dies)
        guest.call('coop.pool tp 60 0')
        guest.call('coop.pool invulnfor 120')
        got = host.call('coop.pool borrow poison graft')['out'].strip()
    else:
        guest.call('coop.pool tp 6 0')
        guest.call('coop.pool invulnfor 120')  # (the host's own clouds knocked it out during the control, lab 20261112-070110)
        got = host.call(f'coop.pool borrow poison graft p{gid}')['out'].strip()
    print('  host: ' + got, flush=True)
    ck(got.startswith('borrowed'), f'the {a.who}\'s cell has the poison part in the host\'s game')
    if not got.startswith('borrowed'):
        return ck.result()
    def made_by():  # the game's cloud maker 0xE76660 calls that made a cloud of that cell (host's game)
        st = host.call(f'coop.pool borrow status x p{gid}')['out']
        m = re.search(r'clouds_made=(\d+)' if a.who == 'host' else r'puppet_clouds=(\d+)', st)
        return int(m.group(1)) if m else None
    seen = clouds(host)
    who = 'host' if a.who == 'host' else gid
    m0 = made_by()
    made = new_clouds(host, seen, 15, who)
    print(f'  control: {len(made)} new clouds next to the {a.who}\'s cell', flush=True)
    m1 = made_by()
    print(f'  control: the game made {m1 - m0 if m0 is not None and m1 is not None else "?"} clouds of the {a.who}\'s cell', flush=True)
    ck(m0 is not None and m1 is not None and m1 > m0, f'control: the host\'s game makes clouds of the {a.who}\'s cell ({m0} -> {m1})')
    ko_con = host if a.who == 'host' else guest
    if a.who == 'guest':
        guest.call('coop.pool invulnfor 0.05')  # vulnerable again for the knockout
        time.sleep(0.5)
    print(f'  {a.who}: ' + ko_con.call('coop.pool selfhit 50')['out'].strip(), flush=True)
    down = wait_downed(ko_con, 1)
    ck(down, f'the {a.who} is knocked out')
    t0 = time.time()
    new_clouds(host, seen, 1.0, who)  # (made in the frame of the knockout)
    m2 = made_by()
    during = new_clouds(host, seen, 8.0, who)
    m3 = made_by()
    still = wait_downed(ko_con, 1, 0.5)
    print(f'  while knocked out ({time.time() - t0:.1f} s): {len(during)} new clouds {sorted(during)[:8]}, still out: {still}',
          flush=True)
    print('  host: ' + ' '.join(x for x in host.call('coop.pool')['out'].splitlines() if x.startswith('cell_c1')), flush=True)
    for x in TRACE:
        print(f'    cloud t+{x[0]} {x[1]} at {x[2]}, the knocked-out cell {x[3]}, the host {x[4]}: {x[5]}', flush=True)
    print('  host: ' + host.call(f'coop.pool borrow status x p{gid}')['out'].strip()[:300], flush=True)
    ck(down and still, f'the {a.who} stayed knocked out while it was watched')
    ck(m2 is not None and m3 == m2, f'the host\'s game makes no cloud of the knocked-out {a.who}\'s cell ({m2} -> {m3} in 8 s)')
    if a.who == 'host':  # (next to a guest the host's own clouds can drift by: printed only)
        ck(not during, f'no new poison cloud next to the knocked-out {a.who} in the host\'s game ({len(during)} new in 8 s)')
    host.call('coop.pool borrow off' if a.who == 'host' else f'coop.pool borrow off x p{gid}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
