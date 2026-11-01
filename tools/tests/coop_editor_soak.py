#!/usr/bin/env python3
"""Many evolution votes with their editors in one session, and the games' address space (audit item 24).

Player report (BUG-0371): the host's game crashed in the creature editor after ~12 h of co-op (RenderWare
raster, out of memory: a 32-bit game has 2-4 GB of address space; a leak per editor visit or a fragmenting
address space ends in a failed allocation in an editor or a load). The other tests enter an editor once or
twice. Here: CYCLES rounds of the host's `coop.vote editor`, the guest's "I'm ready", both games in their
editor, "done" on both, back in the stage; every SHARE_EVERY rounds the host sends its world again
(`coop.share`: a save and a load on the guest). After every round each game walks its address space
(`coop.diag mem`: free MB and the largest free block, the block a planet or an editor needs).

Checks: every round reaches both editors and comes back; per game the least-squares loss of free address
space over the rounds (the first WARM rounds left out: the first editor visit loads its resources) is at
most SLOPE_MAX MB per round and the largest free block never falls under BLOCK_MIN MB; both games run
on without a fault; the per-minute `perf:` lines of the game logs are printed (the lab's as-slope gate
judges them too).

  coop_editor_soak.py [--cycles N] [--share-every K]
  SOAK_CYCLES=N: the number of rounds (default 32)
  SOAK_PLANT_MB=N: the host's game reserves N MB more address space every round (`coop.diag reserve`, a
  planted leak: must FAIL)"""
import argparse
import os
import re
import time

import lane
from coop_puppet_after_events import ev_editor
from loggates import PERF
from twoinst import Checks, shot, start, wait_guest_in

SLOPE_MAX = 8.0   # MB lost per round
BLOCK_MIN = 128   # MB: the largest free block
WARM = 3


def mem(con):
    m = re.search(r'mem: (\d+) of (\d+) MB free, largest block (\d+) MB', con.call('coop.diag mem')['out'])
    return (int(m.group(1)), int(m.group(3))) if m else (None, None)


def slope(ys):
    xs = list(range(len(ys)))
    if len(ys) < 3:
        return 0.0
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else 0.0


def perf_lines(path, offset):
    out = []
    try:
        with open(path, 'rb') as f:
            f.seek(offset)
            for raw in f.read().decode('utf-8', 'replace').splitlines():
                m = PERF.search(raw)
                if m:
                    out.append((raw[:12], int(m.group(5)), int(m.group(7))))
    except OSError:
        pass
    return out


def size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cycles', type=int, default=int(os.environ.get('SOAK_CYCLES', '32') or 32))
    ap.add_argument('--share-every', type=int, default=4)
    a = ap.parse_args()
    plant = int(os.environ.get('SOAK_PLANT_MB', '0') or 0)
    host, guest = start('creature')
    ck = Checks()
    logs = {'host': lane.game_log(lane.HOST), 'guest': lane.game_log(lane.GUEST)}
    offs = {k: size(p) for k, p in logs.items()}
    samples = {'host': [], 'guest': []}
    done = 0
    t0 = time.time()
    for i in range(a.cycles):
        sub = Checks()
        ev_editor(host, guest, sub)
        if not sub.ok:
            ck(False, f'round {i + 1}: both games went into their editor and came back')
            break
        if a.share_every and (i + 1) % a.share_every == 0:
            host.call('coop.share')
            time.sleep(5)
            if not wait_guest_in(host, guest, timeout=90):
                ck(False, f'round {i + 1}: the guest loads the host\'s shared world')
                break
        if plant:
            print('  PLANT: ' + host.call(f'coop.diag reserve {plant}')['out'].strip(), flush=True)
        done += 1
        for who, con in (('host', host), ('guest', guest)):
            samples[who].append(mem(con))
        print(f'  round {i + 1} ({time.time() - t0:.0f} s): host {samples["host"][-1]}, guest {samples["guest"][-1]} '
              f'(free MB, largest block MB)', flush=True)
    ck(done == a.cycles, f'{done} of {a.cycles} rounds of vote, editors and done (shares every {a.share_every})')
    shot(host, 'editor_soak_host')
    shot(guest, 'editor_soak_guest')
    for who in ('host', 'guest'):
        s = [x for x in samples[who] if x[0] is not None]
        free = [x[0] for x in s[WARM:]]
        sl = slope(free)
        low = min((x[1] for x in s), default=None)
        perf = perf_lines(logs[who], offs[who])
        print(f'  {who}: free {s[0][0] if s else "?"} -> {s[-1][0] if s else "?"} MB over {len(s)} rounds, '
              f'{-sl:.1f} MB lost per round after round {WARM}; largest block {s[0][1] if s else "?"} -> '
              f'{s[-1][1] if s else "?"} MB (lowest {low})', flush=True)
        print(f'  {who}: perf lines (time, free MB, largest block MB): {perf}', flush=True)
        ck(len(free) >= 3 and -sl <= SLOPE_MAX, f'{who}: the address space does not run away ({-sl:.1f} <= {SLOPE_MAX} MB per round)')
        ck(low is not None and low >= BLOCK_MIN, f'{who}: the largest free block stays at {BLOCK_MIN} MB or more ({low} MB)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
