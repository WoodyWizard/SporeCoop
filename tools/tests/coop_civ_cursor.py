#!/usr/bin/env python3
"""Civilization: the sync-check does not call a moving pointer "apart" (BUG-0380), and a pointer
that stands still is where the other game shows it.

In the RTS stages a player is a pointer: the planet point under the mouse, which also moves when
the camera moves. The partner's copy follows ~0.1 s plus half a round trip behind, so while the
pointer moves the copy trails by tens of metres, and the support sync-check (10 m for players)
logged `MISMATCH ... p: 1 apart (p0 23.5)` (548 times in campaigns, Civ `p apart` in 29 results).

1. Two games on civ. The host's mouse rests on the planet (move 720 540).
2. The host moves its mouse round a circle over the planet for 9 s (a player's mouse is never still
   for long) and asks the sync-check now (`coop.diag sync now`, recheck 3 s later, both inside).
   Printed: how far the guest's copy of the host's pointer trails (coop.sync p0 on both games).
3. The mouse stops: 3 s later the guest's copy of the host's pointer is within 10 m of the host's
   own pointer (what the guest sees), and a sync-check now is OK.
PASS: no sync-check MISMATCH on the host while the pointer moves, step 3.

SPORE_FIX_OFF=diag.rts_cursor: FAILS (a MISMATCH p0 apart while the mouse moves).

  SPORE_LANE=9 python3 tools/tests/coop_civ_cursor.py
"""
import math
import re
import time

from twoinst import Checks, start


def p0(con):
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) >= 4 and f[0] == 'p0':
            return tuple(float(x) for x in f[1:4])
    return None


def counts(con):
    out = con.call('coop.diag sync')['out']
    m = re.search(r'checks (\d+) ok (\d+) differs (\d+) mismatch (\d+)', out)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0, 0)


def main():
    host, guest = start('civ')
    ck = Checks()
    host.call('coop.diag sync 600')  # no check of its own in between: only the ones asked below
    host.call('move 720 540')
    time.sleep(3)
    a, b = p0(host), p0(guest)
    print(f'  pointer at rest: host {a}, guest sees {b}', flush=True)
    ck(a and b, 'both games have the host\'s pointer (coop.sync p0)')
    c0 = counts(host)

    trail = []
    t0 = time.time()
    asked = False
    k = 0
    while time.time() - t0 < 9:
        if not asked and time.time() - t0 > 1.5:
            host.call('coop.diag sync now')
            asked = True
        ang = k * 0.35
        k += 1
        host.call(f'move {720 + int(220 * math.cos(ang))} {540 + int(160 * math.sin(ang))}')
        if k % 3 == 0:
            a, b = p0(host), p0(guest)
            if a and b:
                trail.append(math.dist(a, b))
        time.sleep(0.03)
    host.call('move 720 540')
    trail.sort()
    if trail:
        print(f'  while the mouse moves: the guest\'s copy trails median {trail[len(trail) // 2]:.1f} m, '
              f'max {trail[-1]:.1f} m ({len(trail)} samples)', flush=True)
    ck(trail and trail[-1] > 2, f'the host\'s pointer moved with the mouse ({trail[-1] if trail else 0:.1f} m)')
    time.sleep(4)
    c1 = counts(host)
    print(f'  sync-check while the mouse moves: checks {c1[0] - c0[0]}, ok {c1[1] - c0[1]}, differs {c1[2] - c0[2]}, '
          f'mismatch {c1[3] - c0[3]}', flush=True)
    ck(c1[0] > c0[0], 'the sync-check ran while the mouse moved')
    ck(c1[3] == c0[3], f'no sync-check MISMATCH for the moving pointer ({c1[3] - c0[3]})')

    a, b = p0(host), p0(guest)
    d = math.dist(a, b) if a and b else 1e9
    print(f'  pointer at rest again: host {a}, guest sees {b}: {d:.1f} m', flush=True)
    ck(d < 10, f'the guest shows the host\'s resting pointer where it is ({d:.1f} m)')
    host.call('coop.diag sync now')
    time.sleep(6)
    c2 = counts(host)
    print(f'  sync-check at rest: checks {c2[0] - c1[0]}, ok {c2[1] - c1[1]}, mismatch {c2[3] - c1[3]}', flush=True)
    ck(c2[1] > c1[1] and c2[3] == c1[3], 'the sync-check at rest is OK (the pointer is compared again)')
    host.call('coop.diag sync on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
