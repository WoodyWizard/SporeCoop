#!/usr/bin/env python3
"""Cell: a guest that swims on while the host's game stands still gets the host's cells around its new place (BUG-0927).

The 0.7.12 final check's campaign (lab 20261111-044257): the host stood in a caption for 110 s (its simulation
stopped), the fourth player swam 100 m on and had 12-13 % of the host's 159-164 cells around it (850 mirrors, none on
its screen): the host chose each guest's cells (within 120 m of the guest, its nearest 900) with the view centres of
its last tile pass, and the game runs no tile pass while its simulation stands.

1. Two players in Cell, food 300 on the host. The guest jumps 150 m away from the host and waits: the host populates
   the tiles around it; the guest has the host's cells around it there (coverage as the campaign measures it).
2. The host opens its pause menu (Esc): its simulation stands.
3. The guest jumps back next to the host (20 m), where the host's own cells are. After 8 s it must have the host's
   cells around it (coverage at least 85 % of the host's cells within 60 m, measured up to 3 times), and cells of
   the host's world on its screen.
4. The host resumes; no game faulted.
SPORE_FIX_OFF=cell.send_centres must FAIL (the guest still gets the cells around the place 150 m away).

  SPORE_LANE=5 python3 tools/tests/coop_cell_host_paused.py
"""
import re
import time

import uifind
from coop_multi import measure
from twoinst import Checks, start


def num(con, key, cmd='coop.pool'):
    m = re.search(r'\b' + key + r'=(-?[\d.]+)', con.call(cmd)['out'])
    return float(m.group(1)) if m else None


def coverage(host, guest, label, tries=3):
    cov, total = 1.0, 0
    for k in range(tries):
        missed = {}
        _, cov, total, _ = measure(host, guest, missed=missed)
        print(f'  [{label}] coverage {cov:.0%} of {total} host cells within 60 m of the guest, '
              f'mirrors {num(guest, "mirrors")}, tile pass behind {num(host, "tile_pass_behind")} m', flush=True)
        if total >= 5 and cov >= 0.85:
            break
        if k + 1 < tries:
            time.sleep(3)
    return cov, total


def main():
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30 and (num(guest, 'mirrors') or 0) < 10:
        time.sleep(1)
    host.call('coop.pool food 300')
    time.sleep(10)
    print('  guest:', guest.call('coop.pool tp 150 0')['out'].strip()[:100], flush=True)
    time.sleep(20)
    cov, total = coverage(host, guest, 'away, the host runs')
    ck(total < 5 or cov >= 0.85, f'1. 150 m away, the host running: the guest has the host\'s cells around it ({cov:.0%} of {total})')
    host.call('key esc')
    time.sleep(2)
    paused = uifind.shown(host, 'Resume Game')
    ck(bool(paused), f'2. the host\'s pause menu is open ({paused})')
    passes0 = num(host, 'tile_passes')
    print('  guest:', guest.call('coop.pool tp 20 0')['out'].strip()[:100], flush=True)
    time.sleep(8)
    passes1 = num(host, 'tile_passes')
    print(f'  host tile passes while paused: {passes0} -> {passes1}', flush=True)
    cov, total = coverage(host, guest, 'back at the host, the host paused')
    ck(total >= 20, f'3. the host has cells around the guest\'s new place ({total})')
    ck(total >= 5 and cov >= 0.85,
       f'3. the host paused, the guest back next to it: the guest has the host\'s cells around it ({cov:.0%} of {total})')
    for _ in range(3):
        if not uifind.shown(host, 'Resume Game'):
            break
        uifind.press(host, 'Resume Game')
        time.sleep(1.0)
    time.sleep(5)
    cov, total = coverage(host, guest, 'the host resumed')
    ck(total >= 5 and cov >= 0.85, f'4. after the host resumed the guest has the host\'s cells around it ({cov:.0%} of {total})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
