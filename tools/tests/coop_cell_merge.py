#!/usr/bin/env python3
"""Cell stage, four players, the host's caps raised: over 2048 live cells and the broadphase merge (BUG-0718, class BUG-0432).

SweepPrune::MergeAxis (0xBBAF10, every frame from Simulator::Cell::SimUpdate) parks the endpoints it
displaces in a 4096-entry queue on its stack with no bound check: a game with more than ~2048 live
cells overruns it, faults at 0xBBB07B and corrupts its stack and heap (BUG-0295; lab 20261030-035318).
The mod's own spawn points keep the pools below that with soft budgets (cell.budget); nothing guarded
the merge itself. The hard guard (cell.sap_merge) merges an axis over 4096 endpoints with its queue on
the heap.

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py).
1. The host's population caps above what the merge holds (`coop.pool budget`, what any spawn the
   budgets do not see does: the game's loot and spawns, a missed budget), the food progress up (--food),
   the guests at three corners of a square of side --far: the host populates a screen of tiles around
   each. Reached: the host's live cells (the pool header) over --cells (the pool holds 4096).
2. For --seconds s with the pool that full: the host's own cell swims to a point and back (its game
   simulates), checked from the game's own position.
3. No caught fault in any game (the lab checks the logs too).
SPORE_FIX_OFF=cell.sap_merge must FAIL (faults at 0xBBB07B, the host's cell stops).

  SPORE_LANE=6 python3 tools/tests/coop_cell_merge.py [--food 300] [--far 200] [--cells 2200] [--seconds 40]
"""
import argparse
import re
import time

from party import Party
from twoinst import Checks


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def own_pos(p):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', p.call('coop.pool'))
    return (float(m.group(1)), float(m.group(2))) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--food', type=int, default=300)
    ap.add_argument('--far', type=float, default=200)
    ap.add_argument('--cells', type=int, default=2300)
    ap.add_argument('--seconds', type=float, default=40)
    ap.add_argument('--wait', type=float, default=150)
    a = ap.parse_args()
    ck = Checks()
    p = Party.start('cell', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        print('  ids', p.ids(), flush=True)
        h = p.host
        # the host's caps over what the merge holds (as the game's own loot and spawns overshoot them; the
        # pool's own 4096 slots are not the case here: a full pool breaks the game elsewhere, lab 20261108-031701)
        print('  host:', h.call(f'coop.pool budget {a.cells + 300} {a.cells + 500}').strip(), flush=True)
        print('  host:', h.call(f'coop.pool food {a.food}').strip()[:120], flush=True)
        time.sleep(8)
        for g, (dx, dy) in zip(p.guests, ((a.far, 0), (0, a.far), (a.far, a.far))):
            print(f'  {g.name}:', g.call(f'coop.pool tp {dx} {dy} 0').strip()[:100], flush=True)
        peak, t0, k = 0, time.time(), 0
        while time.time() - t0 < a.wait:
            for g in p.guests:  # new tiles at the edges of each partner's screen
                g.call(f'coop.pool goto {(-1) ** k * 25} {(-1) ** (k // 2) * 15}')
            k += 1
            time.sleep(4)
            out = h.call('coop.pool')
            peak = max(peak, num(out, 'peak') or 0)
            print(f'  {time.time() - t0:3.0f}s: cells ' + ', '.join(f'{x.name} {num(x.call("coop.pool"), "cells")}' for x in p.all)
                  + f' (host peak {peak})', flush=True)
            if (num(out, 'cells') or 0) >= a.cells + 100:
                break
        print('  host:', h.call('coop.pool sap').strip()[:300], flush=True)
        ck(peak > a.cells, f'1. the host held over {a.cells} live cells (peak {peak}; the merge queue holds ~2048)')
        moved, t0, k = [], time.time(), 0
        while time.time() - t0 < a.seconds:
            start = own_pos(h)
            h.call(f'coop.pool goto {(-1) ** k * 12} 0')
            time.sleep(5)
            end = own_pos(h)
            if start and end:
                moved.append(round(abs(end[0] - start[0]), 1))
            k += 1
        print(f'  host cell moved (m per 5 s): {moved}', flush=True)
        out = h.call('coop.pool')
        print(f'  host after: cells={num(out, "cells")} peak={num(out, "peak")}', flush=True)
        print('  host:', h.call('coop.pool sap').strip()[:300], flush=True)
        h.call('coop.pool budget')
        ck(len(moved) >= 4 and sum(1 for m in moved if m > 3) * 2 >= len(moved),
           f'2. the host\'s game simulates with the full pool: its cell swims ({moved})')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
