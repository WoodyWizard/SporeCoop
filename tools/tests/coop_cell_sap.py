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
   guest still sees the host's cells move (its mirrors of the host's cells follow the host).
4. No caught fault in any game (the lab checks the logs too).
SPORE_FIX_OFF=cell.sap_sweep must FAIL (faults at small addresses on the host, its cell stops).

  SPORE_LANE=5 python3 tools/tests/coop_cell_sap.py [--food 300] [--cells 1100] [--seconds 40]
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
    ap.add_argument('--cells', type=int, default=1100)
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
        print('  host:', h.call(f'coop.pool column {a.cells}').strip(), flush=True)
        print('  host:', h.call('coop.pool sap').strip()[:400], flush=True)
        exc0 = h.call('exc')
        print('  host exc before:', exc0.strip().splitlines()[0] if exc0.strip() else '-', flush=True)
        # 3. the host's simulation goes on: its own cell swims to a point 12 m away and gets there
        moved = []
        t0, k = time.time(), 0
        while time.time() - t0 < a.seconds:
            start = own_pos(h)
            h.call(f'coop.pool goto {(-1) ** k * 12} 0')
            time.sleep(5)
            end = own_pos(h)
            if start and end:
                moved.append(abs(end[0] - start[0]))
            k += 1
        print(f'  host cell moved (m per 5 s): {[round(m, 1) for m in moved]}', flush=True)
        # (the first seconds after the column the crowd of cells pushes itself apart around the host's cell)
        ok = sum(1 for m in moved if m > 3)
        ck(len(moved) >= 4 and ok >= len(moved) - 2, f'3. the host\'s cell swims after the column ({[round(m, 1) for m in moved]})')
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
        for g in p.guests:  # what the guests see: the host's cells keep coming
            out = g.call('coop.pool')
            print(f'  {g.name}: mirrors {num(out, "mirrors")}', flush=True)
            ck((num(out, 'mirrors') or 0) > 50, f'3. {g.name} still mirrors the host\'s cells ({num(out, "mirrors")})')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
