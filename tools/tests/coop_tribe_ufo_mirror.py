#!/usr/bin/env python3
"""BUG-1543: Tribe guest sees the host's visiting drama UFO spaceship.

The host starts its Tribe UFO drama event the game's own way (`coop.drama start 2`); drama.cpp blocks
the guest's own UFO event in Tribe stage (BUG-0486), while player_rules.cpp mirrors the host's UFO to
the guest via K_UFO (BUG-1543). Checked in each game's own object list (`coop.rules ufo`):
  1. within 5 s the guest's game has a mirrored UFO spaceship within 3 m of the host's UFO;
  2. across 8 samples over 4 s the guest's UFO tracks the host's UFO within 3 m (and follows its flight);
  3. the guest's own Drama Manager started no UFO event (`ufo started=0`);
  4. 0 faults on host and guest.
SPORE_FIX_OFF=tribe.ufo_mirror fails 1 and 2 (guest receives no K_UFO in Tribe and sees no UFO).

  SPORE_LANE=3 python3 tools/tests/coop_tribe_ufo_mirror.py
"""
import math
import re
import sys
import time

from twoinst import Checks, shot, start

NEAR = 3.0


def ships(con):
    out = con.call('coop.rules ufo')['out']
    pts = [
        tuple(map(float, m.groups()))
        for m in re.finditer(r'^ufo \S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out, re.M)
    ]
    return pts, out


def nearest(p, qs):
    return min((math.dist(p, q) for q in qs), default=1e9)


def main():
    host, guest = start(fixture='tribe')
    ck = Checks()
    time.sleep(4)
    hs0, _ = ships(host)
    gs0, _ = ships(guest)
    print(f'  before: host ships {hs0}, guest ships {gs0}', flush=True)

    out = host.call('coop.drama start 2')['out']
    print('  host drama: ' + out.strip().replace('\n', ' | '), flush=True)

    t0 = time.time()
    hs, gs = [], []
    while time.time() - t0 < 5.0:
        hs, _ = ships(host)
        gs, _ = ships(guest)
        if hs and gs:
            break
        time.sleep(0.25)
    print(f'  after {time.time() - t0:.1f} s: host ships {hs}, guest ships {gs}', flush=True)
    ck(bool(hs), f'host (control): Tribe drama UFO event made a ship ({hs})')
    if not hs:
        ck.faults(host=host, guest=guest)
        return ck.result()

    d0 = nearest(hs[0], gs)
    ck(bool(gs) and d0 <= NEAR,
       f'guest shows the host\'s Tribe UFO within 5 s ({d0:.2f} m if present, <= {NEAR} m; guest={gs})')
    shot(guest, 'tribe_ufo_mirror_guest')

    worst, n = 0.0, 0
    first_g, last_g = None, None
    first_h, last_h = None, None
    for _ in range(10):
        hs, _ = ships(host)
        gs, _ = ships(guest)
        if not hs:
            break
        d = nearest(hs[0], gs)
        if first_h is None:
            first_h = hs[0]
        last_h = hs[0]
        if gs:
            if first_g is None:
                first_g = gs[0]
            last_g = gs[0]
        print(f'    sample {n}: host {hs} guest {gs}: {d:.2f} m', flush=True)
        worst, n = max(worst, d), n + 1
        time.sleep(0.5)

    h_disp = math.dist(first_h, last_h) if first_h and last_h else 0.0
    g_disp = math.dist(first_g, last_g) if first_g and last_g else 0.0
    print(f'  {n} samples: max error {worst:.2f} m, host moved {h_disp:.2f} m, guest moved {g_disp:.2f} m', flush=True)
    ck(n > 0 and worst <= NEAR,
       f'guest UFO tracks host UFO in Tribe ({worst:.2f} m max in {n} samples, <= {NEAR} m)')

    dg = guest.call('coop.drama')['out']
    m = re.search(r'ufo +started=(\d+)', dg)
    ck(not m or m.group(1) == '0',
       f'guest own Drama Manager started no UFO event ({m and m.group(0)})')

    _, gout = ships(guest)
    _, hout = ships(host)
    print('  guest rules ufo: ' + gout.strip().replace('\n', ' | '), flush=True)
    print('  host rules ufo: ' + hout.strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
