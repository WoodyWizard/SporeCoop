#!/usr/bin/env python3
"""Cell: a guest's own cell that goes NaN is put back, and no tile is populated from NaN view boxes
(BUG-0299).

Seen in the 4-player campaign_short (lab 20261030-053709, fix/w10-cell-4p): a guest's own cell
position became NaN with no NaN mirror around (BUG-0292's guard dropped none), its view boxes
followed, PopulateTiles filled the tile pool with garbage (a fault at PopulateTiles+0x1EF) and the
next level-up crashed in PopulateTile+0x8A (RemoveCellsOfOldScale). The cause of the NaN is not
known; this test makes it (`coop.pool nanself`: the own cell and its soft-body particles NaN).

1. The guest's own cell goes NaN: within 3 s its game shows it at a finite position again
   (`pos=` of `coop.pool`, the game's cell +0x4C) and it still mirrors the host's cells.
2. The host's food goes to 1000 (level-ups: RemoveCellsOfOldScale repopulates every tile): both
   games answer, the guest's position stays finite; the lab fails the test on any fault or crash.
SPORE_FIX_OFF=cell.nan_guard must FAIL (the position stays NaN, faults in PopulateTiles).

--all (BUG-0307, suite cell_nan_all): `coop.pool nanself all` also makes the rotation and shape
matrices and the particles' rest offsets NaN, as left by the first heal in lab 20261030-104149
(26 NaN episodes in 6 minutes). 3. Over the next 15 s the own cell does not go NaN again
(`own_nf=` of `coop.pool`: finite -> not finite transitions, at most 1) and stays finite.
SPORE_FIX_OFF=cell.nan_heal_all must FAIL check 3.

  SPORE_LANE=2 python3 tools/tests/coop_cell_nan.py
"""
import argparse
import math
import re
import time

from twoinst import Checks, start


def pos(con):
    out = con.call('coop.pool')['out']
    m = re.search(r'\bpos=\(([^ )]+) ([^ )]+)', out)
    try:
        return (float(m.group(1)), float(m.group(2))) if m else None
    except ValueError:
        return (math.nan, math.nan)


def finite(p):
    return p is not None and all(math.isfinite(v) for v in p)


def num(con, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    t0 = time.time()
    while time.time() - t0 < 30 and (num(guest, 'mirrors') or 0) < 10:
        time.sleep(1)
    p0 = pos(guest)
    ck(finite(p0) and (num(guest, 'mirrors') or 0) >= 10, f'the guest plays with the host\'s cells ({p0}, mirrors {num(guest, "mirrors")})')
    nf0 = num(guest, 'own_nf') or 0
    print('  guest:', guest.call('coop.pool nanself' + (' all' if a.all else ''))['out'].strip(), flush=True)
    time.sleep(3)
    p1 = pos(guest)
    print(f'  guest after 3 s: pos {p1}, nan_heals={num(guest, "nan_heals")} nan_pop_skips={num(guest, "nan_pop_skips")}', flush=True)
    ck(finite(p1), f'1. the guest\'s own cell is at a finite position again ({p1})')
    ck(finite(p1) and finite(p0) and math.dist(p0, p1) < 30, f'1. ... where it was ({p0} -> {p1})')
    ck((num(guest, 'mirrors') or 0) >= 10, f'1. the guest still mirrors the host\'s cells ({num(guest, "mirrors")})')
    if a.all:
        bad = 0
        for _ in range(15):
            time.sleep(1)
            bad += not finite(pos(guest))
        nf = (num(guest, 'own_nf') or 0) - nf0
        print(f'  guest: own_nf +{nf}, non-finite samples {bad} of 15, nan_heals={num(guest, "nan_heals")}', flush=True)
        ck(nf <= 1 and bad == 0, f'3. the own cell does not go NaN again (episodes {nf}, non-finite samples {bad} of 15)')
    print('  host:', host.call('coop.pool food 1000')['out'].strip()[:80], flush=True)
    time.sleep(15)
    p2 = pos(guest)
    ck(guest.call('coop.pool').get('ok') is not False and finite(p2), f'2. after the level-ups the guest answers at a finite position ({p2})')
    ck((num(guest, 'mirrors') or 0) >= 10, f'2. the guest mirrors the host\'s cells ({num(guest, "mirrors")})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
