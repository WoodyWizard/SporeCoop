#!/usr/bin/env python3
"""BUG-9292: with partners apart the host's Cell pool stays at about a single player's number of cells.

The owner (2026-11-20, friends build): "too many creatures spawn and it lags a lot". With the guest apart the host's game
populated two areas and kept up to 1100 live cells (a game alone: ~580, at most ~800), and its frame rate followed.

The host's game's own live cell count (the cell pool's count, `coop.pool` cells=) every 2 s:
  1. the guest alone in the world is left out: the host alone, 60 s: the single-player count (printed);
  2. the guest joins and swims away (teleported 120 units): 90 s: the host's count never passes 950 (the budget 800
     plus one frame's tiles) and its average stays within 1.5x of the single-player average;
  3. the guest still finds food on its screen (its own `coop.pool see`: plants or meat) at least once;
  4. no faults.
--old (`coop.fix cell.pop_budget_solo off`, twin pop_budget_off): step 2 passes 950 (up to 1100).

  SPORE_LANE=N python3 tools/tests/coop_cell_pop_budget.py [--old]
"""
import re
import statistics
import sys
import time

import lane
from twoinst import Checks, start


def cells(con):
    m = re.search(r'\bcells=(\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def sample(con, secs):
    out = []
    t0 = time.time()
    while time.time() - t0 < secs:
        n = cells(con)
        if n is not None:
            out.append(n)
        time.sleep(2)
    return out


def main():
    host, guest = start('cell', own_progress=True)
    ck = Checks()
    if '--old' in sys.argv:
        host.call('coop.fix cell.pop_budget_solo off')
    guest.call('coop.leave')
    time.sleep(5)
    solo = sample(host, 60)
    print(f'  alone: avg {statistics.mean(solo):.0f} max {max(solo)}', flush=True)
    guest.call(lane.JOIN)
    for _ in range(60):
        if 'Cell' in guest.call('game.mode')['out'] and 'puppet' in host.call('coop.pool')['out']:
            break
        time.sleep(3)
    time.sleep(15)
    guest.call('coop.pool tp 120 0')
    food = 0
    two = []
    t0 = time.time()
    while time.time() - t0 < 90:
        two += sample(host, 10)
        see = guest.call('coop.pool see 80')['out']
        food += sum(1 for ln in see.splitlines() if ln.startswith('see ') and ln.split()[2] in ('plant', 'meat'))
    print(f'  two players apart: avg {statistics.mean(two):.0f} max {max(two)}', flush=True)
    ck(max(two) <= 950, f'2: the host keeps at most a single player\'s budget (max {max(two)})')
    ck(statistics.mean(two) <= 1.5 * statistics.mean(solo), '2: on average within 1.5x of the host alone')
    ck(food > 0, f'3: the guest finds food on its screen ({food} sightings)')
    host.call('coop.fix cell.pop_budget_solo on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
