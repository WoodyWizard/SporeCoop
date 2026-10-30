#!/usr/bin/env python3
"""Cell: a guest at a low frame rate shows the host's moving cells where they are (BUG-0282).

Measured for BUG-0275 (4 players, lane 6): a guest forced to 15 fps showed 283 host cells about 2 m
behind their way (error / speed ~0.66 s), at 65 fps none. The guest steered a rigid mirror only
by its velocity (`v = host vel + (want - cur) * 2`, a jump beyond 3 m) and the game's cell physics
moves a cell less per second at a long frame, so the mirror settled behind.

1. Two games on the cell fixture; the guest runs at ~15 fps (`frame.limit 66`), both play with the
   play_cell bot (the cells around them chase, flee and wander).
2. --rounds times, 3 s apart: the guest's copy of every host cell within 60 m of it is compared with
   the host's path around the guest's sample (coop_multi.measure).
Checked: the guest ran at a low frame rate (median fps < 25), enough cells were compared, and at
most 0.5 % of them are more than 1.2 m off (BUG-0275's limit of the measurement).
A guard, not a fix test: on main 5d7f20e (2026-10-30) the trail did not come back (2 players at
15 fps: 0 of 16 616 cells over 1.2 m, worst 0.08 m; 4 players at 15 / 30 fps after level-ups:
p90 <= 0.05 m), so BUG-0282 is not-reproduced and there is no fix to switch off.

  SPORE_LANE=2 python3 tools/tests/coop_cell_lowfps.py [--rounds 30] [--frame-ms 66]
"""
import argparse
import random
import re
import threading
import time

import coop_multi as cm
import lane
import play_cell
from sporectl import Console
from twoinst import Checks, start

FAR = 1.2   # m
SHARE = 0.005


def fps(con):
    m = re.search(r'fps=([\d.]+)', con.call('frame')['out'])
    return float(m.group(1)) if m else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=30)
    ap.add_argument('--frame-ms', type=int, default=66)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    print('  guest: frame.limit', a.frame_ms, guest.call(f'frame.limit {a.frame_ms}')['out'].strip(), flush=True)
    stop = []

    def run(b):
        while not stop:
            try:
                b.step(time.time(), play_cell.pool(b.c))
                time.sleep(0.25)
            except Exception as e:  # noqa: BLE001 (a bot hiccup must not end the measurement)
                print('  bot error', repr(e), flush=True)
                time.sleep(1)
    for name, inst, pid in (('Alice', lane.HOST, 0), ('Bob', lane.GUEST, 1)):
        b = play_cell.Player(name, Console(inst, timeout=30), pid, 1 - pid, random.Random(7 + pid))
        threading.Thread(target=run, args=(b,), daemon=True).start()
    time.sleep(10)
    errs, rates, worst = [], [], (0.0, '')
    try:
        for r in range(a.rounds):
            time.sleep(3)
            e, cov, total, w = cm.measure(host, guest)
            c = e.get('c', [])
            errs += c
            rates.append(fps(guest))
            far = sum(1 for x in c if x > FAR)
            if w.get('c') and w['c'][0] > worst[0]:
                worst = w['c']
            print(f'  r{r}: {len(c)} cells, {far} over {FAR} m, p90 {cm.pct(c, 0.9) if c else 0:.2f} m, '
                  f'coverage {cov:.0%}, guest fps {rates[-1]:.0f}', flush=True)
    finally:
        stop.append(1)
    guest.call('frame.limit auto')
    rates.sort()
    med = rates[len(rates) // 2] if rates else 0
    far = sum(1 for x in errs if x > FAR)
    print(f'  total: {len(errs)} cells compared, {far} over {FAR} m ({far / max(1, len(errs)):.2%}), '
          f'worst {worst[0]:.2f} m {worst[1]}, guest fps median {med:.0f}', flush=True)
    ck(0 < med < 25, f'the guest ran at a low frame rate (median {med:.0f} fps)')
    ck(len(errs) >= 20 * a.rounds, f'enough host cells compared ({len(errs)})')
    ck(far <= SHARE * len(errs), f'at most {SHARE:.1%} of the host\'s cells are over {FAR} m off on the guest '
       f'({far} of {len(errs)})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
