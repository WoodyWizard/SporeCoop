#!/usr/bin/env python3
"""BUG-0686: the Creature Drama Manager's UFO visit with a ship this game has no model for (the gfx factory 0xE994D0).

The visit's ship comes from the host's Sporepedia cache (`Pollination.package`). A cache that holds UFO summaries only
(lab lanes 2 and 3; lane 5's of 2026-11-04, BUG-0546) and a ship whose model was never baked into GraphicsCache.package
leave the ship gfx without a model; the factory then wrote GetModel()+4: a fault at 0xE99524 (data 0x4) on the host,
seen at the campaign's Creature -> Tribe advance (lab 20261107-171750, 20261107-175737-2). The test starts the visit
the game's own way (`coop.drama start 2`, as creature_ufo_mirror does) until one ship gfx is made without a model, up
to TRIES visits (the ship is picked at random from the cache; before each visit game.ufo_model forgets a known
ship model an earlier visit had, `coop.ufoforget`, as in a Creature game that has none), then lets the visit go on for HOLD s:
  1. no fault on the host (0xE99524 at the start, 0xE9A908 later), the games answer;
  2. the case happened: a ship gfx without a model (the guard's counter, or the fault with the fix off); else SKIP.
SPORE_FIX_OFF=game.ufo_gfx fails 1.

  SPORE_LANE=N python3 tools/tests/coop_creature_ufo_nomodel.py [--cache bug0546|lane|<file>]
  --cache  the host's Sporepedia cache (default bug0546: lane 5's cache without a complete UFO, see
           coop_space_npc_model.py); `lane`: the lane's own. The lane's own file comes back at the end.
"""
import os
import re
import shutil
import sys
import time

import twoinst  # noqa: F401 (first: tools/ on sys.path)
import lane
from coop_space_npc_model import bad_cache
from twoinst import Checks, alive, shot, start

TRIES = 5
HOLD = 40.0


def faults(con):
    out = con.call('exc')['out'].splitlines()
    m = re.search(r'total faults: (\d+)', out[0] if out else '')
    return (int(m.group(1)) if m else 0), out


def gfx_null(con):
    m = re.search(r'ship gfx made without a model (\d+) \(their messages skipped (\d+)\)', con.call('coop.status')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


def visit_active(con):
    m = re.search(r'^ +ufo +started=\d+ .*active=(\d)', con.call('coop.drama')['out'], re.M)
    return bool(m and m.group(1) == '1')


def ufo_fault(lines):
    return [ln for ln in lines if re.search(r'0x00E99524|0x00E9A908', ln)]


def run():
    host, guest = start(fixture='creature')
    ck = Checks()
    time.sleep(3)
    f0, _ = faults(host)
    case = False
    for i in range(TRIES):
        n0 = gfx_null(host)[0]
        host.call('coop.ufoforget')  # a Creature game has no own ship: no known ship model for game.ufo_model
        out = host.call('coop.drama start 2')['out']
        print(f'  visit {i + 1}: ' + ' | '.join(ln for ln in out.splitlines() if 'ufo' in ln)[:200], flush=True)
        time.sleep(5)
        f, lines = faults(host)
        n = gfx_null(host)[0]
        if n > n0 or ufo_fault(lines) or f > f0:
            print(f'  visit {i + 1}: a ship gfx without a model (guard {n0} -> {n}, faults {f0} -> {f})', flush=True)
            case = True
            break
        t = time.time()  # the visit's ship had a model: wait for the visit to end, then the next one
        while time.time() - t < 150 and visit_active(host):
            time.sleep(3)
        print(f'  visit {i + 1}: its ship had a model; the visit ended after {time.time() - t:.0f} s', flush=True)
    if case:
        shot(host, 'ufo_nomodel_host')
        t = time.time()
        while time.time() - t < HOLD and alive(lane.HOST):
            time.sleep(2)
    f1, lines = faults(host)
    print('  host faults: ' + ' | '.join(ln.strip() for ln in lines[:4])[:400], flush=True)
    print(f'  guard: ship gfx without a model / messages skipped {gfx_null(host)}', flush=True)
    ck(alive(lane.HOST), 'the host game still answers')
    ck(f1 == f0 and not ufo_fault(lines), f'no fault on the host from the ship without a model ({f0} -> {f1})')
    ck.faults(host=host, guest=guest)
    if ck.ok and not case:
        print(f'RESULT: SKIP (every ship of {TRIES} visits had a model: the case did not happen)', flush=True)
        return 0
    return ck.result()


def main():
    arg = sys.argv[sys.argv.index('--cache') + 1] if '--cache' in sys.argv else 'bug0546'
    cache = bad_cache() if arg == 'bug0546' else None if arg == 'lane' else arg
    if arg == 'bug0546' and not cache:
        print('RESULT: SKIP (no copy of the BUG-0546 cache: out/fixtures/bug0546-Pollination.package)', flush=True)
        return 0
    own = os.path.join(lane.HOST_DIR, 'Pollination.package')
    keep = own + '.bug0686-own'
    if cache:  # before the games start (the game reads it at its start); the lane's own file comes back
        lane.sh('kill')
        if os.path.isfile(own) and not os.path.isfile(keep):
            shutil.copy2(own, keep)
        shutil.copy(cache, own)
        print(f'  host cache: {cache}', flush=True)
    try:
        return run()
    finally:
        if cache and os.path.isfile(keep):
            lane.sh('kill')
            shutil.move(keep, own)


if __name__ == '__main__':
    raise SystemExit(main())
