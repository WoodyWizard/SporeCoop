#!/usr/bin/env python3
"""Cell: a full soft-body particle pool does not break the guest's game (BUG-0338).

The cells' soft-body particles come from one pool of 1500 (cCellGame+0x4108). A full pool hands out
-1, the game keeps it in the cell's particle list, reads and writes particle -1 (8 bytes before the
pool's block) and frees it, which puts -1 on the pool's free list: the next allocations read a
garbage free-list head and fault every frame at 0x00E4DBD9 (1332 faults on a host in cell_sap,
lab 20261031-124047-fix-w22-cell; 0x00E4E384 on a guest).

1. Two players in Cell, food 300 on the host. The guest's pool is filled to 1499 used particles
   (`coop.pool particles 1499`, real allocations by the game's own ParticleAlloc).
2. --rounds times the guest's mirrors are removed (`coop.pool zap 5000`) and the host's next state
   makes them again; the pool is filled to 1499 again right after each removal, so every new mirror
   near the camera asks for particles from a full pool, every removal frees what it got.
3. Checked on the guest (the game's own state): no cell holds -1 among its particles, the pool's
   free-list head stays inside the pool, no caught fault, the pool's count (the game's header) never below 0 or
   over 1500, its own cell stays where it was (no jump) and moves at most 20 m in a round, the
   mirrors come back. The own cell is invulnerable: bitten and knocked out by the crowd it moved
   5-18 m in a round (a respawn), with BUG-0355's fixes or without, and without a full pool too
   (--no-fill); without the fixes the shared spare also flung it 64 m to 1e7 m in 4 of 12 runs
   (BUG-0355 follow-up, docs/re/cell.md#cell-gfx-pool).
SPORE_FIX_OFF=cell.particles must FAIL; SPORE_FIX_OFF=cell.spare,cell.gfx_full,cell.bare_still
fails in about 1 run of 3 (--repeat).

  SPORE_LANE=8 python3 tools/tests/coop_cell_particles.py [--rounds 16]
"""
import argparse
import math
import re
import time

from twoinst import Checks, lab_mark, start


MAX_STEP = 20.0  # m in one round (~3 s): shoves by the crowd reach 11 m (invulnerable, fixes on); the shared NaN spare flung it 64 m to 1e7 m


def out(con, line):
    return con.call(line).get('out') or ''


def num(text, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', text)
    return int(m.group(1)) if m else None


def own_pos(con):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', out(con, 'coop.pool'))
    return (float(m.group(1)), float(m.group(2))) if m else None


def faults(con):
    m = re.search(r'total faults: (\d+)', out(con, 'exc'))
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=16)
    ap.add_argument('--no-fill', action='store_true', help='the mirrors churn without a full pool (control: what the churn alone does)')
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30 and (num(out(guest, 'coop.pool'), 'mirrors') or 0) < 10:
        time.sleep(1)
    host.call('coop.pool food 300')
    time.sleep(10)
    if not a.no_fill:
        print('  guest:', out(guest, 'coop.pool particles 1499').strip(), flush=True)
    p0 = own_pos(guest)
    counts, jumps, steps, minus1, head_bad = [], [], [], [], []
    last = p0
    lab_mark('sync-force', 'on')  # the rounds remove the guest's mirrors on purpose (BUG-0379)
    for i in range(a.rounds):
        guest.call('coop.pool invuln 1')  # a knock-out's respawn moved it 18 m (not this test's subject)
        guest.call('coop.pool zap 5000')
        if not a.no_fill:
            guest.call('coop.pool particles 1499')  # the removals gave particles back: full again for the new mirrors
        time.sleep(2.5)
        s = out(guest, 'coop.pool sap')
        counts.append(num(s, 'particles'))
        minus1.append(num(s, 'minus1_cells') or 0)
        head_bad.append(num(s, 'free_head_bad') or 0)
        p = own_pos(guest)
        if p and p0:
            jumps.append(math.dist(p, p0))
        if p and last:
            steps.append(math.dist(p, last))
        last = p or last
        pa = out(guest, 'coop.pool parts')
        print(f'  round {i + 1}: particles={num(s, "particles")} spare={num(s, "spare")} bad_frees={num(s, "bad_frees")} '
              f'minus1_cells={minus1[-1]} free_head_bad={head_bad[-1]} mirrors={num(out(guest, "coop.pool"), "mirrors")} own cell {jumps[-1] if jumps else "?"} m from the start, '
              f'{steps[-1] if steps else "?"} m this round | own_n={num(pa, "own_n")} own_spare={num(pa, "own_spare")} '
              f'stripped={num(pa, "stripped")} spare_freed={num(pa, "spare_freed")} near_bare={num(pa, "near_bare")} nan_cells={num(pa, "nan_cells")}',
              flush=True)
    lab_mark('sync-force', 'off')
    ok = [c for c in counts if c is not None]
    ck(len(ok) == len(counts) and min(ok) >= 0 and max(ok) <= 1500,
       f'3. the particle pool\'s count stays in 0..1500 ({min(ok) if ok else "?"}..{max(ok) if ok else "?"})')
    ck(max(minus1) == 0, f'3. no cell holds the pool\'s -1 among its particles (most: {max(minus1)} cells)')
    ck(max(head_bad) == 0, f'3. the pool\'s free list stays inside the pool (bad in {sum(head_bad)} of {len(head_bad)} rounds)')
    ck(bool(jumps) and max(jumps) < 30, f'3. the guest\'s own cell stays where it was (at most {max(jumps) if jumps else "?":.1f} m away)')
    # BUG-0355 follow-up: a step of 13-18 m in one round (the cell flung by the crowd around it)
    ck(bool(steps) and max(steps) < MAX_STEP, f'3. the guest\'s own cell moves at most {MAX_STEP:.0f} m in a round '
                                              f'(most {max(steps) if steps else "?":.1f} m)')
    m = num(out(guest, 'coop.pool'), 'mirrors') or 0
    ck(m > 100, f'3. the guest mirrors the host\'s cells ({m})')
    for name, con in (('host', host), ('guest', guest)):
        n = faults(con)
        ck(n == 0, f'3. {name}: no caught fault ({n})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
