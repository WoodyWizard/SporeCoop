#!/usr/bin/env python3
"""Cell: a host creature a guest swallows whole is gone in the host's game too (BUG-0827).

SwallowWhole 0xE7D880 destroys a swallowed cell that has a model (every creature) only at the end of its
"cell_death_eaten" animation (event 7), not when the swallow starts. On a guest the swallowed cell is a
mirror of a host creature; its late DestroyCell was not counted as the guest's meal (h_Destroy counts a
mirror destroy as eaten only within 1 s of a touch) and no ACT_GONE went out: the guest got the meal, the
host's creature lived on and could be eaten again.

1. The guest finds a host creature next to it (`coop.pool kill - hit` names the nearest, one bite).
2. The guest's mouth swallows it whole: the game's interaction executor 0xE7E7F0 with kind 10
   (`coop.pool swallow c<key>`, StartEatSwallow -> event 1 -> SwallowWhole), three creatures in turn.
3. Within 6 s of each swallow the creature is dying or gone in the HOST's game (the game's own cell).
SPORE_FIX_OFF=cell.swallow_gone must FAIL at 3 (the creature lives on in the host's game).

  SPORE_LANE=3 python3 tools/tests/coop_cell_swallow_gone.py
"""
import re
import time

from coop_cell_guest_hit import host_cell, num
from twoinst import Checks, start


def gone(st):
    return 'no such cell' in st or 'dying=1' in st or (num(st, 'hp') or 1) <= 0


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    done, tried, used = 0, 0, set()
    for rnd in range(3):
        key, r = None, ''
        for _ in range(15):
            r = guest.call('coop.pool kill - hit')['out']
            m = re.match(r'bit (c\d+) ', r)
            if m and 'dying' not in r and m.group(1) not in used:
                key = m.group(1)
                break
            guest.call(f'coop.pool tp {(_ % 3 - 1) * 20} 15')
            time.sleep(1)
        if not key:
            continue
        used.add(key)
        guest.call(f'coop.pool swimto {key} tp')
        time.sleep(1.5)
        st0 = host_cell(host, key)
        r = guest.call(f'coop.pool swallow {key} 10')['out'].strip()
        tried += 1
        t0, ok = time.time(), False
        while time.time() - t0 < 6:
            st = host_cell(host, key)
            if gone(st):
                ok = True
                break
            time.sleep(0.25)
        print(f'  swallow {rnd + 1} {key}: {r} | host before: {st0.strip()[:120]} | gone in the host\'s game: {ok} '
              f'after {time.time() - t0:.1f} s', flush=True)
        done += ok
    print('  guest: ' + ' '.join(x for x in guest.call('coop.pool')['out'].splitlines() if x.startswith('cell_c1')), flush=True)
    print('  guest why: ' + guest.call('coop.pool why')['out'].strip().replace('\n', '; ')[:400], flush=True)
    ck(tried >= 2, f'the guest swallowed {tried} host creatures')
    ck(tried and done == tried, f'every creature the guest swallowed whole is gone in the host\'s game ({done} of {tried})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
