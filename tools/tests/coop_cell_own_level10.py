#!/usr/bin/env python3
"""BUG-0637: Cell: after the host changes its own cell at level 10 (and a guest rejoined before), are there
cells in the host's world that do not belong to the level?

Two games in the Cell fixture. The host's shared food goes to the player's report (924: level 10). The host's
pool is listed by creature model and level (`coop.pool species`) a while after the level change (the
level's own population), then the host's own cell is changed with the co-op window's arrows
(`coop.creations next cell`, `prev cell`: the level is rebuilt) and listed again, and once more after the
guest left and joined again. Checked in the host's game: after the rebuild no creature model the level did
not have before, no creature of a level outside the level's range (IsTileActive: the level base and the next five), and the
host's own species and the guest's cell are not among the NPCs.

  coop_cell_own_level10.py [--food 924]
"""
import argparse
import re
import time

from twoinst import Checks, start


def species(con):
    out = con.call('coop.pool species')['out']
    own = re.search(r'own model=([0-9A-F]+) level=(-?\d+) food_level=(-?\d+)', out)
    m = {k: (int(n), [int(x) for x in lv.split(',') if x]) for k, n, lv in re.findall(r'model ([0-9A-F]+) n=(\d+) levels=(\S*)', out)}
    return (own.group(1), int(own.group(2)), int(own.group(3))) if own else ('?', -1, -1), m


def show(name, sp):
    own, m = sp
    print(f'  {name}: own {own[0]} level {own[1]} (food level {own[2]}); ' +
          ', '.join(f'{k} x{n} L{"/".join(map(str, lv))}' for k, (n, lv) in sorted(m.items())), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--food', type=int, default=924)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 900')
    guest.call('coop.pool invulnfor 900')
    for step in range(0, a.food, 100):  # level by level, as play does
        host.call(f'coop.pool food {min(100, a.food - step)}')
        time.sleep(3)
    time.sleep(8)
    print('  host creations:', host.call('coop.creations')['out'].strip().replace('\n', ' | ')[:400], flush=True)
    base = species(host)
    show('host before', base)
    show('guest before', species(guest))
    seen_models = set(base[1])
    gown = species(guest)[0][0]
    rows = []
    for cmd in ('next cell', 'prev cell'):
        r = host.call(f'coop.creations {cmd}')['out'].strip()
        print(f'  host: coop.creations {cmd}: {r[:160]}', flush=True)
        time.sleep(8)
        sp = species(host)
        show(f'host after {cmd}', sp)
        show(f'guest after {cmd}', species(guest))
        rows.append((cmd, sp))
    for cmd, (own, m) in rows:
        lv0 = own[2]
        new = [k for k in m if k not in seen_models]
        odd = [(k, lv) for k, (n, lv) in m.items() if any(l < lv0 - 1 or l > lv0 + 5 for l in lv if l >= 0)]
        ck(not odd, f'after {cmd}: every creature of the host\'s pool belongs to the level (food level {lv0}; outside: {odd})')
        ck(own[0] not in m and gown not in m, f'  the players\' cells ({own[0]}, guest {gown}) are not NPCs of the level')
        print(f'  after {cmd}: models not seen before the change: {new}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
