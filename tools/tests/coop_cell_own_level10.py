#!/usr/bin/env python3
"""BUG-0637: Cell: after the host changes its own cell at level 10 (and a guest rejoined before), are there
cells in the host's world that do not belong to the level?

Two games in the Cell fixture. The host's shared food goes to the player's report (924: level 10). The host's
pool is listed by creature model and level (`coop.pool species`) a while after the level change (the
level's own population), then the host's own cell is changed with the co-op window's arrows
(`coop.creations choose cell`: a premade cell, then the first one again, as the window's arrows do: the level is
rebuilt each time) and listed again.
Checked in the host's game: after the rebuild no creature of a level outside the level's range (IsTileActive:
the level base and the next five), no creature model the level did not have before other than the player's own
species (the game populates NPCs of the player's species, also in a single player's game), and no NPC of the
previous own cell is left.

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
    orig = base[0][0]
    for cmd in ('choose cell BAF1AD40', f'choose cell {orig}'):  # a premade cell (the test profiles have no own ones), then back
        r = host.call(f'coop.creations {cmd}')['out'].strip()
        print(f'  host: coop.creations {cmd}: {r[:160]}', flush=True)
        time.sleep(8)
        sp = species(host)
        show(f'host after {cmd}', sp)
        show(f'guest after {cmd}', species(guest))
        rows.append((cmd, sp))
    prev = orig
    for cmd, (own, m) in rows:
        lv0 = own[2]
        new = [k for k in m if k not in seen_models and k != own[0]]
        odd = [(k, lv) for k, (n, lv) in m.items() if any(l < lv0 - 1 or l > lv0 + 5 for l in lv if l >= 0)]
        ck(not odd, f'after {cmd}: every creature of the host\'s pool belongs to the level (food level {lv0}; outside: {odd})')
        # the game populates NPCs of the player's own species (8 of the fixture's own cell before any change, as in a
        # single player's game): after a change they are of the new species, none of the previous one is left
        ck(prev == own[0] or prev not in m, f'  no NPC of the previous own cell {prev} is left (own species now {own[0]} x{m.get(own[0], (0,))[0]})')
        ck(not new, f'  no creature model the level did not have before, other than the own species ({new})')
        prev = own[0]
    host.call('coop.creations unchoose cell')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
