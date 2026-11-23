#!/usr/bin/env python3
"""Cell: a guest's meal does not dip out of its food bar while the host's game has not added it yet (CELL-1 of the
external audit, BUG-9316; BUG-0051's symptom again).

Since BUG-0633 (cell.food_in_step) the host adds a guest's meal at its next cell step, but it acknowledged the meal's
number when the message came. The host's food total that went out in between (every 0.5 s) had the number but not the
food: the guest dropped the meal from "on the way" and its food bar fell by the meal for up to 0.5 s (over a level
line: a level down and up). About 3 % of a guest's meals at 60 fps, every meal while the host's cell step stands (a
short pause).

1. Cell fixture, the same food total on both games.
2. Three rounds: the host's queued food waits 1.5 s (`coop.pool foodhold 1.5`: as while its cell step stands; its
   totals go out as usual meanwhile), the guest eats 10 points (`coop.pool food 10`, the game's AddFoodPoints, a
   meal to the host). The guest's food is read every ~0.1 s for 3 s.
3. PASS: once the guest's food had the meal it never fell below that again, and afterwards the host's total has it and
   the guest's equals the host's. SPORE_FIX_OFF=cell.ack_in_step: the guest's food falls by 10 for about a second in
   every round: FAIL.

--shared: the same with the session setting all.shared_dna on (`coop.settings force`; the default is own DNA per
player, 0.7.16).

  SPORE_LANE=4 python3 tools/tests/coop_cell_food_ack.py [--shared]
"""
import re
import sys
import time

import settings_lib
import twoinst

from harness import num, waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def food(con):
    m = re.search(r'^cell\((?:host|guest)\): food=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def main():
    host, guest = start('cell')
    ck = Checks()
    shared = '--shared' in sys.argv
    if shared:
        ck(settings_lib.force(host, [guest], 'all.shared_dna', 'on'), 'the guest plays with all.shared_dna on')
    try:
        return rounds(host, guest, ck)
    finally:
        if shared:
            settings_lib.reset([host, guest], ('all.shared_dna',))
            twoinst.forget_settings()


def rounds(host, guest, ck):
    ck(wait(lambda: (num(guest.call('coop.pool')['out'], 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    ck(wait(lambda: food(host) is not None and food(host) == food(guest), 15), 'the same food total')
    for rnd in range(3):
        h0, g0 = food(host), food(guest)
        if g0 is None or h0 is None:
            ck(False, f'round {rnd + 1}: the food totals are readable')
            break
        print(f'  round {rnd + 1}: host {host.call("coop.pool foodhold 1.5")["out"].strip()}', flush=True)
        guest.call('coop.pool food 10')
        reads, t0 = [], time.time()
        while time.time() - t0 < 3.0:
            f = food(guest)
            if f is not None:
                reads.append((round(time.time() - t0, 2), f))
            time.sleep(0.08)
        vals = [f for _, f in reads]
        k = next((i for i, f in enumerate(vals) if f >= g0 + 10), None)
        low = min(vals[k:]) if k is not None else None
        print(f'  round {rnd + 1}: guest food {g0} -> reads {vals[:40]}', flush=True)
        ck(k is not None, f'round {rnd + 1}: the guest\'s game counts its meal ({g0} -> {max(vals) if vals else None})')
        ck(low is not None and low >= g0 + 10,
           f'round {rnd + 1}: the guest\'s food never fell below its meal while the host added it (low {low}, '
           f'at least {g0 + 10}; dips at {[t for t, f in reads[k or 0:] if f < g0 + 10][:6]} s)')
        same = wait(lambda: (food(host) or 0) >= h0 + 10 and food(host) == food(guest), 10)
        ck(same, f'round {rnd + 1}: the meal is in the host\'s total and the guest has the same ({h0} -> {food(host)}, '
           f'guest {food(guest)})')
        time.sleep(1)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
