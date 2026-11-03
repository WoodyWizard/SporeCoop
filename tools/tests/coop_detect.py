#!/usr/bin/env python3
"""The divergence detector on a fresh session of one stage (docs/sync-architecture-plan.md L0).

Starts the host with the stage's fixture and joins the guest, lets the world run (the guest walks to a
few places near the host in Creature), and compares the host's world with the guest's every --every
seconds (tools/tests/worlddiff.py over `coop.census all`). Each finding is printed as a DETECTOR line
(tools/detector_report.py counts them over runs). The test fails only when a dump is broken (incomplete,
empty, no own position) or a game faults: the findings are data, not a gate.

  coop_detect.py [--fixture creature] [--minutes 3] [--every 20]
"""
import argparse
import json
import math
import random
import time

import worlddiff
from twoinst import Checks, start


def walk(con, rng):
    """the guest's creature walks a few metres somewhere (Creature only; coop.abilities goto)"""
    try:
        out = con.call('coop.census all 30').get('out', '')
        w = worlddiff.World(out)
        if w.me and w.mode == 'Creature':
            x, y, z = w.me
            con.call(f'coop.abilities goto {x + rng.uniform(-25, 25):.1f} {y:.1f} {z + rng.uniform(-25, 25):.1f}')
    except (OSError, ValueError):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--minutes', type=float, default=3.0)
    ap.add_argument('--every', type=float, default=20.0)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    rng = random.Random(seed)
    print(f'detector on {a.fixture}: {a.minutes} min, a comparison every {a.every:.0f} s, seed {seed}', flush=True)
    host, guest = start(a.fixture)
    ck = Checks()
    t_end = time.time() + a.minutes * 60
    n, total, sigs = 0, 0, set()
    time.sleep(10)
    while time.time() < t_end:
        t0 = time.time()
        hw, gw = worlddiff.pair_dump(host, guest)
        dt = time.time() - t0
        n += 1
        if n == 1:
            ck(hw.complete and gw.complete, f'both dumps complete (host {len(hw.rows)} rows, guest {len(gw.rows)} rows, '
                                            f'{dt:.1f} s for both)')
            ck(len(hw.nouns) > 0 and len(gw.nouns) > 0, f'noun lists read (host {len(hw.nouns)} kinds, guest {len(gw.nouns)})')
            ck(hw.me is not None and gw.me is not None, f'own positions (host {hw.me}, guest {gw.me})')
        found = worlddiff.diff(hw, gw)
        print('DETECTOR-SAMPLE ' + json.dumps(dict(guest='guest', stage=hw.mode, gstage=gw.mode, host_rows=len(hw.rows),
                                                   guest_rows=len(gw.rows), findings=len(found),
                                                   skipped=found[0]['sig'] if found and found[0]['sig'] in ('apart', 'nocentre') else '')),
              flush=True)
        for f in found:
            if f['sig'] in ('apart', 'nocentre'):
                continue
            total += 1
            sigs.add(f['sig'])
            print('DETECTOR ' + json.dumps(dict(guest='guest', stage=f['stage'], sig=f['sig'], n=f['n'], text=f['text'])),
                  flush=True)
        walk(guest, rng)
        time.sleep(max(1.0, a.every - (time.time() - t0)))
    print(f'{n} comparison(s), {total} finding(s), {len(sigs)} signature(s): {", ".join(sorted(sigs)) or "none"}', flush=True)
    ck(n >= 2, f'{n} comparisons made')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
