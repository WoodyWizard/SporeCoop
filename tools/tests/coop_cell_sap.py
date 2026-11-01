#!/usr/bin/env python3
"""Cell stage, four players: more than 1024 cells open at once in the game's broadphase sweep (BUG-0337).

SweepPrune::UpdatePairs (0xBBB450, called by Simulator::Cell::SimUpdate every frame) sweeps the
cell broadphase along x with the objects it has seen the min endpoint of but not the max in an
int[1024] on its stack, with no bound check. The 1025th open object overwrites its return address:
`ret` jumps to a broadphase object id (0x196, 0x4D6, ...), SporeKit and the game catch the fault,
the rest of SimUpdate is skipped, every frame, with a 500-750 ms hitch each (lab 20261031-054758
campaign#1, Carol: 4314 faults, a desync, then a frozen game after the stage change).

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py).
1. The food progress goes up (`coop.pool food <n>` on the host) to a level with more cells: the
   host holds over --cells live cells.
2. The guests go --far m away (their own areas); the host puts --cells of its cells on one vertical line next to its own cell (`coop.pool column
   <n>`): their x ranges all overlap, so the sweep holds all of them open at once (what a player
   sees as a crowd of cells in one place; the broken order the campaign hit leaves them open the
   same way).
3. For --seconds s, checked on the host: the game's own simulation goes on (the host's cell swims
   to a point and gets there, all but the first two times), its frames come (no hitch over 400 ms in the perf lines), the sweep
   did hold over 1024 open objects (`coop.pool sap` sweep_peak, so the case was reached), and each
   guest's game shows the host's cell swimming (its puppet of the host moves as much).
4. No caught fault in any game (the lab checks the logs too).
SPORE_FIX_OFF=cell.sap_sweep must FAIL (faults at small addresses on the host, its cell stops).

  SPORE_LANE=5 python3 tools/tests/coop_cell_sap.py [--food 300] [--cells 1100] [--seconds 40]
"""
import argparse
import re
import time

import math

from coop_multi import sample
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
    ap.add_argument('--cells', type=int, default=1100)  # at least; all the host's cells but 40
    ap.add_argument('--seconds', type=float, default=40)
    ap.add_argument('--far', type=float, default=200)
    a = ap.parse_args()
    ck = Checks()
    p = Party.start('cell', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        print('  ids', p.ids(), flush=True)
        h = p.host
        if a.food:
            print('  host:', h.call(f'coop.pool food {a.food}').strip()[:120], flush=True)
        cells = 0
        t0 = time.time()
        while time.time() - t0 < 60:  # the level change, then the tiles fill
            time.sleep(4)
            cells = num(h.call('coop.pool'), 'cells') or 0
            print(f'  host cells {cells}', flush=True)
            if cells >= a.cells + 30:
                break
        ck(cells >= a.cells + 30, f'1. the host holds over {a.cells} cells ({cells})')
        # the guests play 200 m away (their own areas, BUG-0174): the column stays out of their
        # mirrors (a guest mirroring such a pack faulted elsewhere once, BUG-0338)
        for g, (dx, dy) in zip(p.guests, ((a.far, 0), (0, a.far), (a.far, a.far))):
            print(f'  {g.name}:', g.call(f'coop.pool tp {dx} {dy} 0').strip()[:100], flush=True)
        time.sleep(8)
        # all the host's cells but 40 (at least --cells): over 1024 must stand in the column at once
        n = max(a.cells, (num(h.call('coop.pool'), 'cells') or 0) - 40)
        print('  host:', h.call(f'coop.pool column {n}').strip(), flush=True)
        print('  host:', h.call('coop.pool sap').strip()[:400], flush=True)
        exc0 = h.call('exc')
        print('  host exc before:', exc0.strip().splitlines()[0] if exc0.strip() else '-', flush=True)
        # 3. the host's simulation goes on: its own cell swims to a point 12 m away and gets there
        moved, seen = [], {g.name: [] for g in p.guests}

        def views():  # where each guest's game shows the host's cell (its puppet, coop.sync key p0)
            return {g.name: sample(g.con).get('p0') for g in p.guests}
        t0, k = time.time(), 0
        while time.time() - t0 < a.seconds:
            start, vstart = own_pos(h), views()
            h.call(f'coop.pool goto {(-1) ** k * 12} 0')
            time.sleep(5)
            end, vend = own_pos(h), views()
            if start and end:
                moved.append(abs(end[0] - start[0]))
            for g in p.guests:
                if vstart[g.name] and vend[g.name]:
                    seen[g.name].append(math.dist(vstart[g.name], vend[g.name]))
            k += 1
        print(f'  host cell moved (m per 5 s): {[round(m, 1) for m in moved]}', flush=True)
        # (the first seconds after the column the crowd of cells pushes itself apart around the host's cell)
        # the crowd pushes the host's cell about: it swims in most intervals, and never jumps (a
        # centre from a broken soft body jumped thousands of metres, BUG-0338)
        # The first interval is left out (the crowd pushing itself apart reached 97 m with every fix
        # on), and one interval in which the host's cell was knocked out and respawned next to a
        # partner may be longer (a respawn moves it by design). A broken soft body's jump was
        # 22685 m and 9934066 m: anything over 1000 m always fails.
        respawned = 'respawned small' in ((h.call('log 400') or '').split('column:')[-1])

        def no_jump(v):
            big = [m for m in v[1:] if m >= 60]
            return max(v) < 1000 and (not big or (respawned and len(big) == 1 and big[0] < 300))
        print(f'  host respawned during the run: {respawned}', flush=True)
        ok = sum(1 for m in moved if m > 3)
        ck(len(moved) >= 4 and ok * 2 >= len(moved) and no_jump(moved),
           f'3. the host\'s cell swims after the column, no jump ({[round(m, 1) for m in moved]})')
        sap = h.call('coop.pool sap')
        print('  host:', sap.strip()[:500], flush=True)
        fix_on = num(sap, 'fix') == 1
        if fix_on:
            ck((num(sap, 'sweep_peak') or 0) > 1024, f'3. the sweep held over 1024 open objects (sweep_peak={num(sap, "sweep_peak")}: the case was reached)')
        else:  # the game's own sweep: what our replay of it counts
            ck((num(sap, 'peak_open') or 0) > 1024, f'3. the sweep holds over 1024 open objects (peak_open={num(sap, "peak_open")}: the case was reached)')
        # the host's own log since the column: one hitch per faulting frame without the fix
        tail = (h.call('log 400') or '').split('column:')[-1]
        hitches = [int(x) for x in re.findall(r'hitch (\d+) ms', tail)]
        print(f'  host hitches since the column: {hitches[:12]}', flush=True)
        ck(sum(1 for x in hitches if x > 400) <= 1, f'3. the host\'s frames keep coming (hitches over 400 ms: {sum(1 for x in hitches if x > 400)})')
        for g in p.guests:  # what the other players see: the host's cell swimming on their screens
            v = seen[g.name]
            ok = sum(1 for m in v if m > 3)  # (as the host's own, a jump on the host is a jump here)
            print(f'  {g.name} sees the host\'s cell move (m per 5 s): {[round(m, 1) for m in v]}', flush=True)
            ck(len(v) >= 4 and ok * 2 >= len(v) and no_jump(v), f'3. {g.name} sees the host\'s cell swim ({[round(m, 1) for m in v]})')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
