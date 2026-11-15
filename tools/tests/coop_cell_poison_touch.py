#!/usr/bin/env python3
"""Cell: a guest swimming through a poison cloud of the host's pool does not end it there (BUG-0835).

On a guest every cell of the host's pool is a mirror; a destroy of a mirror that touched the guest's own
cell in the last second counts as eaten (MSG_CELL_EAT ACT_GONE: the host destroys its cell). The guest's
visibility step destroys every poison cloud outside its active area each frame (0xE78C37, a guest has no
active tiles), so a mirrored cloud the guest swam into was "eaten": the host's cloud ended at once.

1. The host's cell gets a poison part (`coop.pool borrow poison graft`: the game's poison step makes its
   clouds, 3-7 s each); the guest swims next to the host.
2. Each new cloud the guest's game mirrors (`coop.pool see`, kind poison) the guest's cell is put right
   onto (`coop.pool swimto c<key> tp 0`: the game's collision pair with it).
3. 0.8 s later the cloud is still there in the HOST's game (`coop.pool cell <key>`) for nearly all touched
   clouds (a cloud may end by itself: at most a quarter of them gone, at least 6 touched).
Not reproduced (2026-11-12): the guest's game never destroyed a mirrored cloud it touched (`coop.pool why`: none, 12
touches, lab 20261112-065459 on the code without a fix); the fix tried (cell.poison_not_eaten) was taken out again.
Kept as a regression test.

  SPORE_LANE=2 python3 tools/tests/coop_cell_poison_touch.py
"""
import re
import time

from coop_cell_ko_poison import clouds
from coop_cell_puppet_scale import playing, wait
from twoinst import Checks, start


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    got = host.call('coop.pool borrow poison graft')['out'].strip()
    print('  host: ' + got, flush=True)
    ck(got.startswith('borrowed'), 'the host\'s cell has the poison part')
    if not got.startswith('borrowed'):
        return ck.result()
    guest.call('coop.pool tp 3 0')
    time.sleep(2)
    seen, touched, lost, t0 = set(), 0, 0, time.time()
    while time.time() - t0 < 70 and touched < 12:
        new = sorted(clouds(guest) - seen)
        seen |= set(new)
        for key in new[:1]:
            if 'no such cell' in host.call(f'coop.pool cell {key}')['out']:
                continue
            r = guest.call(f'coop.pool swimto {key} tp 0')['out'].strip()
            time.sleep(0.8)
            st = host.call(f'coop.pool cell {key}')['out']
            ended = 'no such cell' in st or 'dying=1' in st
            touched += 1
            lost += ended
            print(f'  {key}: {r}; in the host\'s game 0.8 s later: {"gone" if ended else "there"}', flush=True)
        if not new:
            guest.call('coop.pool tp 3 0')  # back next to the host (its clouds start at its parts)
            time.sleep(0.5)
    print('  guest why: ' + guest.call('coop.pool why')['out'].strip().replace('\n', '; ')[:400], flush=True)
    print('  guest: ' + ' '.join(x for x in guest.call('coop.pool')['out'].splitlines() if x.startswith('cell_c1')), flush=True)
    ck(touched >= 6, f'the guest touched {touched} of the host\'s clouds')
    ck(touched and lost <= touched // 4, f'the touched clouds live on in the host\'s game ({lost} of {touched} gone 0.8 s later)')
    host.call('coop.pool borrow off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
