#!/usr/bin/env python3
"""BUG-0558: the UFO that visits the host's world (the Drama Manager's UFO abduction, kind 2) is seen by the guest.

The host starts its UFO event the game's own way (`coop.drama start 2`, StartKind as the stage's start does); the
event makes its ship 20 m over the host's creature. Checked on the guest's game, in the game's own object list (the
UFO nouns and their spatial position, `coop.rules ufo`):
  1. within 3 s the guest's game has a ship within 3 m of the host's ship, and keeps it there (8 samples over 4 s);
  2. the guest's own Drama Manager started no event (the ship is the host's: creature.guest_events stays);
  3. when the host's ship is gone, the guest's goes within 3 s (if the host's goes within 90 s).
SPORE_FIX_OFF=creature.ufo_mirror fails 1.

  SPORE_LANE=N python3 tools/tests/coop_creature_ufo_mirror.py
"""
import math
import re
import sys
import time

from twoinst import Checks, shot, start

NEAR = 3.0


def ships(con):
    return [tuple(map(float, m.groups())) for m in
            re.finditer(r'^ufo \S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.rules ufo')['out'], re.M)]


def nearest(p, qs):
    return min((math.dist(p, q) for q in qs), default=1e9)


def main():
    host, guest = start(fixture='creature')
    ck = Checks()
    time.sleep(3)
    print(f'  before: host ships {ships(host)}, guest ships {ships(guest)}', flush=True)
    shot(guest, 'ufo_mirror_guest_before')
    out = host.call('coop.drama start 2')['out']
    print('  host: ' + out.strip().replace('\n', ' | '), flush=True)
    t0 = time.time()
    hs, gs = [], []
    while time.time() - t0 < 3.0:
        hs, gs = ships(host), ships(guest)
        if hs and gs:
            break
        time.sleep(0.25)
    print(f'  after {time.time() - t0:.1f} s: host ships {hs}, guest ships {gs}', flush=True)
    ck(bool(hs), f'host (control): its UFO event made a ship ({hs})')
    if not hs:
        return ck.result()
    ck(bool(gs) and nearest(hs[0], gs) <= NEAR,
       f'the guest\'s game shows the host\'s ship within 3 s ({nearest(hs[0], gs) if gs else None} m, <= {NEAR} m)')
    shot(guest, 'ufo_mirror_guest')
    worst, n = 0.0, 0
    for _ in range(8):
        hs, gs = ships(host), ships(guest)
        if not hs:
            break
        d = nearest(hs[0], gs)
        print(f'    host {hs} guest {gs}: {d:.2f} m', flush=True)
        if n == 4:
            shot(guest, 'ufo_mirror_guest_2')
        worst, n = max(worst, d), n + 1
        time.sleep(0.5)
    print(f'  {n} samples: the guest\'s ship at most {worst:.2f} m from the host\'s', flush=True)
    ck(n > 0 and worst <= NEAR, f'the guest\'s ship stays at the host\'s ({worst:.2f} m in {n} samples, <= {NEAR} m)')
    dg = guest.call('coop.drama')['out']
    m = re.search(r'ufo +started=(\d+)', dg)
    ck(not m or m.group(1) == '0', f'the guest\'s own Drama Manager started no UFO event ({m and m.group(0)})')
    t1 = time.time()
    while time.time() - t1 < 90 and ships(host):
        time.sleep(1.0)
    if ships(host):
        print('  the host\'s ship stayed 90 s: its end is not judged', flush=True)
    else:
        t2 = time.time()
        while time.time() - t2 < 3.0 and ships(guest):
            time.sleep(0.25)
        g = ships(guest)
        print(f'  host ship gone after {t2 - t1:.0f} s; guest ships 3 s later {g}', flush=True)
        ck(not g, f'the guest\'s ship goes with the host\'s ({g})')
        shot(guest, 'ufo_mirror_guest_after')
    print('  guest: ' + guest.call('coop.rules ufo')['out'].strip().replace('\n', ' | '), flush=True)
    print('  host: ' + host.call('coop.rules ufo')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
