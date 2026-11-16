#!/usr/bin/env python3
"""Cell stage sync over a lossy, slow link (run with SPORE_NET, suite entry cell_net):

  BUG-0032  the host's state comes in unreliable chunks of 14 cells: a lost or late chunk made the
            guest delete those mirrors and create them again 0.1 s later (new pool cells: they
            blinked, lost their facing). The guest's pool index of every host cell stays the same.
  BUG-0051  the host eats, the guest eats over a level line before the host's total (without its
            meal) arrives: the guest's food total must not drop below what it had after its meal
            (before: back under the line, a level down and up again).

SPORE_CELL_FIX=0 switches the fixes off on both games (`coop.pool fix all 0`): the run that must FAIL.
"""
import os
import re
import time

from harness import pool, waiter
from twoinst import Checks, start


def num(out, key, cast=int):
    m = re.search(r'\b' + key + r'=(-?[\d.]+)', out)
    return cast(m.group(1)) if m else None


def food(con):
    return num(pool(con), 'food')


wait = waiter(step=0.5)


def mirror_map(guest):
    return dict(re.findall(r'^map (\d+) (\d+)$', guest.call('coop.pool mirrormap')['out'], re.MULTILINE))


def main():
    fix = os.environ.get('SPORE_CELL_FIX', '1')
    print('  network:', os.environ.get('SPORE_NET', '(none: run with SPORE_NET="delay=250,loss=0.05")'))
    host, guest = start('cell')
    ck = Checks()
    try:
        for con in (host, guest):
            out = con.call(f'coop.pool fix all {fix}')['out']
            ck(f'purge={fix}' in out, f'fixes set to {fix}: {out.strip()}')
        ck(wait(lambda: (num(pool(guest), 'mirrors') or 0) >= 15, 60, 1), f'the guest mirrors the host pool ({num(pool(guest), "mirrors")})')

        # ---- BUG-0032: the local cell of each host cell stays the same through lost chunks ----
        g0 = pool(guest)
        prev, changed, seen = mirror_map(guest), 0, 0
        for _ in range(20):
            time.sleep(1)
            cur = mirror_map(guest)
            for h, local in cur.items():
                if h in prev:
                    seen += 1
                    changed += prev[h] != local
            prev = cur
        g1 = pool(guest)
        print(f'  {seen} samples, {changed} host cells got a new local cell; created {num(g0, "created")} -> {num(g1, "created")}, '
              f'destroyed {num(g0, "destroyed")} -> {num(g1, "destroyed")}, rounds with a lost chunk {num(g1, "rounds_partial")}, '
              f'late chunks {num(g1, "late_chunks")}')
        ck(seen > 100, 'mirrors sampled')
        ck(changed <= 2, f'BUG-0032: lost state chunks leave the mirrors in place ({changed} recreated)')

        # ---- BUG-0051: the host's older total does not take the guest's meal back ----
        dips = []
        for attempt in range(3):
            f = food(host)
            d = (45 - f % 50) % 50
            if d:
                host.call(f'coop.pool food {d}')
            ck(wait(lambda: food(guest) == food(host) and food(host) % 50 == 45, 8), f'both at {food(host)} (5 under a level line)')
            time.sleep(1)
            lv = num(pool(guest), 'target', float)  # the avatar's size: set by the game at a level change
            host.call('coop.pool food 2')
            guest.call('coop.pool food 10')
            after = food(guest)
            low, low_lv, t0 = after, num(pool(guest), 'target', float), time.time()
            while time.time() - t0 < 3:
                p = pool(guest)
                low = min(low, num(p, 'food'))
                low_lv = min(low_lv, num(p, 'target', float))
                time.sleep(0.05)
            hf, gf = food(host), food(guest)
            top = num(pool(guest), 'target', float)
            print(f'  try {attempt + 1}: guest {after} after its meal, lowest {low}; cell size {lv} -> lowest after the meal {low_lv} -> '
                  f'{top}; totals host {hf} guest {gf}')
            dips.append(low < after)
            # (a cell drifting into a mouth can add a few points meanwhile)
            ck(hf == gf and hf >= after + 2, f'the totals agree in the end ({hf}, {gf}, at least {after + 2})')
        ck(not any(dips), f'BUG-0051: the guest total never dropped below its meal ({sum(dips)} of {len(dips)} dipped)')
    finally:
        for con in (host, guest):
            con.call('coop.pool fix all 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
