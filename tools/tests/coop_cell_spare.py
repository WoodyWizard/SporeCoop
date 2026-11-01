#!/usr/bin/env python3
"""Cell: a crowd of cells on screen does not fill the soft-body particle pool (BUG-0355).

4-player campaign_short (lab 20261031-151552, extra1): a guest mirroring 900 of the host's cells
filled the cells' soft-body particle pool (1500) in ordinary play; from then on BUG-0338's spare
slot was shared by hundreds of cells, it went NaN, the NaN spread through every cell holding it, its
own cell (56 of its 64 particles the spare after it grew in the end-of-Cell cinematic) flew
thousands of metres, and its game ran at 3 fps for good: it never left Cell.

Cause: a cell gets a soft body when it comes into the active area (SoftBodyLod 0xE78C00 ->
CreateSoftBody 0xE6D8F0): particles first, then an object from the cell graphics pool (256). A full
graphics pool hands out 0, the cell keeps its particles with no object, the next frame asks again:
4 to 64 particles lost per such cell per frame (DestroyCell frees them only with the object).

1. Two players in Cell, food 300 on the host; the guest mirrors the host's cells.
2. The host puts --crowd of its cells on a disc of --radius m around its own (`coop.pool crowd`):
   over 256 cells in both games' active areas.
3. Checked in both games (the game's own pool and the cells' own lists, `coop.pool orphans` /
   `parts`): no particle is lost (used by the pool but held by no cell), no cell holds the spare
   slot, no cell's position is NaN.
4. The host ends the Cell stage the players' way (food 1000: the guest's cell grows in its own
   cinematic, History, the next stage); the guest keeps over 10 fps all along and follows into the
   Creature stage; no fault.
SPORE_FIX_OFF=cell.gfx_full must FAIL (step 3: ~600-1500 particles lost in each game).

  SPORE_LANE=5 python3 tools/tests/coop_cell_spare.py [--crowd 1000] [--radius 8] [--sample]
"""
import argparse
import re
import time

import lane
from twoinst import Checks, Monitor, alive, start

STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)
MIN_FPS = 10.0
MAX_LOST = 2  # particles the pool counts as used that no cell holds


def out(con, line):
    try:
        return con.call(line).get('out') or ''
    except (OSError, ValueError):
        return ''


def num(text, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', text)
    return int(m.group(1)) if m else None


def mode(con):
    return out(con, 'game.mode')


def fps_over(con, secs=3.0):
    """frames per second over the next secs, from the game's frame counter"""
    def frame():
        m = re.search(r'frame=(\d+)', out(con, 'frame'))
        return int(m.group(1)) if m else None
    f0, t0 = frame(), time.time()
    time.sleep(secs)
    f1, t1 = frame(), time.time()
    return (f1 - f0) / (t1 - t0) if f0 is not None and f1 is not None else 0.0


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sample', action='store_true', help='print where the guest\'s main thread spends its time')
    ap.add_argument('--crowd', type=int, default=1000, help='the host puts this many of its cells around its own')
    ap.add_argument('--radius', type=float, default=8.0)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    wait(lambda: (num(out(guest, 'coop.pool'), 'mirrors') or 0) >= 50 or None, 40)
    host.call('coop.pool food 300')
    time.sleep(10)
    f0 = fps_over(guest)
    print(f'  guest: {num(out(guest, "coop.pool"), "mirrors")} mirrors, {f0:.1f} fps', flush=True)
    print('  guest (before):', out(guest, 'coop.pool orphans').strip(), flush=True)

    # 2. a crowd in both games' active areas
    print('  host:', out(host, f'coop.pool crowd {a.crowd} {a.radius}').strip(), flush=True)
    lost, spare, nan = {}, {}, {}
    for k in range(3):
        time.sleep(4)
        for name, con in (('host', host), ('guest', guest)):
            o, p = out(con, 'coop.pool orphans'), out(con, 'coop.pool parts')
            print(f'  {name} {4 * (k + 1)} s: cells={num(out(con, "coop.pool"), "cells")}', p.strip(), flush=True)
            print(f'  {name} {4 * (k + 1)} s:', o.strip().replace('\n', ' |'), flush=True)
            lost[name] = max(lost.get(name, 0), num(o, 'orphans') or 0)
            spare[name] = max(spare.get(name, 0), (num(p, 'spare_cells') or 0) + (1 if (num(p, 'own_spare') or 0) else 0))
            nan[name] = max(nan.get(name, 0), num(p, 'nan_cells') or 0)
    f1 = fps_over(guest)
    print(f'  guest with the crowd: mirrors={num(out(guest, "coop.pool"), "mirrors")} {f1:.1f} fps', flush=True)
    if a.sample:
        print('  guest sample (crowd):', out(guest, 'coop.sample 3000 30'), flush=True)
    for name in ('host', 'guest'):
        ck(lost[name] <= MAX_LOST, f'3. {name}: the particle pool loses no particles with a crowd on screen '
                                   f'(at most {lost[name]} used by the pool and held by no cell)')
        ck(spare[name] == 0, f'3. {name}: no cell shares the spare particle slot ({spare[name]} cells)')
        ck(nan[name] == 0, f'3. {name}: no cell\'s position is NaN ({nan[name]} cells)')
    lows = [('crowd', f1)]

    # 4. the end of the Cell stage (coop_cell_leave.py step 2); the guest's fps all along
    host.call('coop.pool food 1000')
    t0 = time.time()
    sampled = False
    while time.time() - t0 < 150 and 'Creature' not in mode(host):
        if not alive(lane.HOST) or not alive(lane.GUEST):
            break
        f = fps_over(guest, 2.0)
        g = out(guest, 'coop.pool')
        lows.append((f'{time.time() - t0:.0f} s{" cine" if "cine=1" in g else ""}', f))
        size = re.search(r'size=([\d.]+)', g)
        print(f'  {time.time() - t0:5.0f} s: guest {f:.1f} fps cine={num(g, "cine")} size={size.group(1) if size else "?"} '
              f'host {mode(host).strip()}', flush=True)
        if not sampled and 'cine=1' in g:
            print('  guest (cinematic):', out(guest, 'coop.pool parts').strip(), flush=True)
            if a.sample:
                print('  guest sample (cinematic):', out(guest, 'coop.sample 3000 30'), flush=True)
            sampled = True
        if 'cine=1' in out(host, 'coop.pool'):
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(2)
            continue
        if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(2)
    ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host).strip()})')
    for _ in range(3):  # the guest's game after the cinematic, while it waits for the host's world
        if 'Creature' in mode(guest):
            break
        lows.append(('after', fps_over(guest)))
    worst = min(lows, key=lambda x: x[1])
    ck(worst[1] >= MIN_FPS, f'4. the guest keeps over {MIN_FPS:.0f} fps with the crowd and through the cinematic '
                            f'(lowest {worst[1]:.1f} at {worst[0]}; before the crowd {f0:.1f})')
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 150),
       f'4. the guest follows into the Creature stage ({mode(guest).strip()})')
    if alive(lane.HOST) and alive(lane.GUEST):
        ck.faults(host=host, guest=guest)
    else:
        ck(False, 'both games run on')
        for e in Monitor.events:
            print('  (monitor)', e)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
