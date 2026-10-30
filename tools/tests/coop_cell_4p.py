#!/usr/bin/env python3
"""Cell stage with four players far apart and close together (BUG-0295, BUG-0298).

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py). The host keeps the world
populated around every partner (BUG-0174, `cell.partner_tiles`) and sends each guest the cells
around it. With four players the pools grew past what the game's cell broadphase holds (~2048
cells: a 4096-endpoint stack buffer in the sweep-and-prune merge 0xBBAF10): a fault at 0xBBB07B
on every game, then crashes (DestroyCell at 0xA81FE6, a worker thread at 0xA479B7) and frozen
games in the 4-player campaign (lab 20261030-035318).

1. The food progress goes up (`coop.pool food <n>` on the host, --food) to a level with more
   cells per screen.
2. Apart: the guests stand at three corners of a square of side --far around the host (each its
   own screen of tiles on the host), --seconds s, swimming a little.
3. Close: the guests stand --near m from the host (their 120 m state ranges overlap: each guest
   used to get nearly every host cell), --seconds s.
Checked on every game after each phase: its live cell count (the pool header, the game's own
count) stayed under 1800 the whole time (`peak=`, the highest seen per frame), each guest sees
the host's cells around itself (coverage within 60 m, as the campaign measures it), cells of
the host's world on each guest's screen (not an empty pool, BUG-0174), and no fault in any log (the lab).
SPORE_FIX_OFF=cell.budget must FAIL (a pool over 2000, the fault at 0xBBB07B).

--jumps S (BUG-0307 hunt): before the phases, Carol runs at --jump-ms per frame and jumps to a new
place 150-250 m from the host every 3 s for S s (the guest's reported centre jumped like this while
its own cell was NaN in lab 20261030-104149); her free address space is sampled and printed, and
4. it must not fall by more than 300 MB.

  SPORE_LANE=2 python3 tools/tests/coop_cell_4p.py [--food 300] [--far 200] [--near 50] [--seconds 60]
"""
import argparse
import re
import time

from coop_multi import measure
from party import Party
from twoinst import Checks

CAP = 1800  # the budget keeps the host near 1300 (spikes of ~250 seen: loot, the game's own spawns); the game's broadphase overflows at ~2048


def pool(p):
    return p.call('coop.pool')


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def on_screen(p):
    """(cells of the host's world on this guest's screen, how many of them are food)"""
    seen = [line.split() for line in p.call('coop.pool see 300').splitlines() if line.startswith('see ')]
    return len(seen), sum(1 for f in seen if len(f) > 2 and f[2] in ('plant', 'meat'))


def report(p, label):
    out = pool(p)
    tiles = next((line for line in out.splitlines() if line.startswith('cell_tiles')), '')
    print(f'  [{label}] {p.name}: cells={num(out, "cells")} peak={num(out, "peak")} level={num(out, "level")} '
          f'| {tiles[:260]}', flush=True)
    return num(out, 'peak') or 0


def phase(ck, p, label, seconds):
    t0 = time.time()
    k = 0
    while time.time() - t0 < seconds:
        # every guest swims a little (new tiles at the edges of its screen)
        for g in p.guests:
            g.call(f'coop.pool goto {(-1) ** k * 15} {(-1) ** (k // 2) * 10}')
        k += 1
        time.sleep(4)
        print(f'  [{label}] {time.time() - t0:3.0f}s: cells ' + ', '.join(
            f'{x.name} {num(pool(x), "cells")}' for x in p.all), flush=True)
    peaks = {x.name: report(x, label) for x in p.all}
    for name, pk in peaks.items():
        ck(0 < pk < CAP, f'[{label}] {name}: the game\'s cell pool stays under {CAP} (peak {pk})')
    for g in p.guests:
        # measured up to 3 times 2 s apart, as the campaign does (a tile populated on the host right
        # then reaches the guest with the next state rounds)
        for _ in range(3):
            _, cov, total, _ = measure(p.host.con, g.con)
            if total < 5 or cov >= 0.85:
                break
            print(f'  [{label}] {g.name}: coverage {cov:.0%} of {total}, measured again in 2 s', flush=True)
            time.sleep(2)
        ck(total < 5 or cov >= 0.85, f'[{label}] {g.name} has the host\'s cells around it (coverage {cov:.0%} of {total})')
        n, food = on_screen(g)  # (food itself is rare on some screens at level 4: not asserted)
        ck(n > 0, f'[{label}] {g.name} sees cells of the host\'s world on its screen ({n}, food {food})')


def jump(ck, p, a):
    import math
    import random
    c = p.byname('Carol')
    print('  Carol: frame.limit', c.call(f'frame.limit {a.jump_ms}').strip()[:60], flush=True)
    rng, vm, t0, k = random.Random(5), [], time.time(), 0
    while time.time() - t0 < a.jumps:
        ang, r = rng.uniform(0, 2 * math.pi), rng.uniform(150, 250)
        c.call(f'coop.pool tp {r * math.cos(ang):.0f} {r * math.sin(ang):.0f} 0')
        time.sleep(3)
        k += 1
        if k % 5 == 0:
            out = pool(c)
            vm.append(num(out, 'vm_free_mb'))
            print(f'  [jumps] {time.time() - t0:3.0f}s: Carol free {vm[-1]} MB, mirrors {num(out, "mirrors")}, '
                  f'own_nf {num(out, "own_nf")}, created {num(out, "created")}', flush=True)
            print('  [jumps]', c.call('coop.pool census').strip(), flush=True)
    c.call('frame.limit auto')
    time.sleep(10)
    print('  [jumps] after:', c.call('coop.pool census').strip(), flush=True)
    ok = [v for v in vm if v is not None]
    ck(len(ok) >= 2 and ok[0] - min(ok) <= 300, f'4. Carol keeps her free address space while jumping ({ok[:1]} -> {min(ok) if ok else "?"} MB)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--food', type=int, default=300)
    ap.add_argument('--far', type=float, default=200)
    ap.add_argument('--near', type=float, default=50)
    ap.add_argument('--seconds', type=float, default=60)
    ap.add_argument('--jumps', type=float, default=0)
    ap.add_argument('--jump-ms', type=int, default=300)
    a = ap.parse_args()
    ck = Checks()
    p = Party.start('cell', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        ids = p.ids()
        print('  ids', ids, flush=True)
        if a.food:
            print('  host:', p.host.call(f'coop.pool food {a.food}').strip()[:120], flush=True)
            time.sleep(8)  # the level change takes a few frames (the world scale moves)
        report(p.host, 'level')
        if a.jumps:
            jump(ck, p, a)
        for g, (dx, dy) in zip(p.guests, ((a.far, 0), (0, a.far), (a.far, a.far))):
            print(f'  {g.name}:', g.call(f'coop.pool tp {dx} {dy} 0').strip()[:100], flush=True)
        phase(ck, p, 'apart', a.seconds)
        for g, (dx, dy) in zip(p.guests, ((a.near, 0), (0, a.near), (-a.near, 0))):
            print(f'  {g.name}:', g.call(f'coop.pool tp {dx} {dy} 0').strip()[:100], flush=True)
        phase(ck, p, 'close', a.seconds)
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
